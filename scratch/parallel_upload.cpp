
/*******************************************************************************
 * BSD 3-Clause License
 *
 * Copyright (c) 2020, Ugo Varetto
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions inputFile source code must retain the above copyright
 *notice, this list inputFile conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list inputFile conditions and the following disclaimer in the
 *documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name inputFile the copyright holder nor the names inputFile
 *its contributors may be used to endorse or promote products derived from this
 *software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

// Parallel file upload to S3 servers

#include <aws_sign.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
#include <future>
#include <iostream>
#include <regex>
#include <set>
#include <stdexcept>
#include <vector>

#include "lyra/lyra.hpp"
#include "response_parser.h"
#include "utility.h"
#include "webclient.h"

using namespace std;

//------------------------------------------------------------------------------
struct Config {
    bool showHelp = false;
    string s3AccessKey;
    string s3SecretKey;
    string endpoint;
    string bucket;
    string key;
    string file;
    string credentials;
    string awsProfile;
    int maxRetries = 2;
    int jobs = 1;
    string memoryMapping = "none";
    // when pre-loading data the number of threads used to load data
    // can be different from the number of threads used to send data
    // int loadJobs = -1;
    // int dataTransferJobs = -1;
};

void Validate(const Config& config) {
    if (config.s3AccessKey.empty() && !config.s3SecretKey.empty() ||
        config.s3SecretKey.empty() && !config.s3AccessKey.empty()) {
        throw invalid_argument(
            "ERROR: both access and secret keys have to be specified");
    }
    if (config.jobs < 1) {
        throw invalid_argument(
            "ERROR: number of jobs must be greater than one, " +
            to_string(config.jobs) + " provided");
    }
    if (config.maxRetries < 1) {
        throw invalid_argument(
            "ERROR: number of retries must be greater than one, " +
            to_string(config.maxRetries) + " provided");
    }
#ifdef VALIDATE_URL
    const URL url = ParseURL(config.endpoint);
    if (url.proto != "http" && url.proto != "https") {
        throw invalid_argument(
            "ERROR: only 'http' and 'https' protocols supported");
    }
    regex re(R"((\w+\.)*\w+\.\w+)");
    if (!regex_match(url.host, re)) {
        throw invalid_argument(
            "ERROR: invalid endpoint format, should be "
            "http[s]://hostname[:port]");
    }
    if (url.port > 0xFFFF) {
        throw invalid_argument(
            "ERROR: invalid port number, should be in range[1-65535]");
    }
#endif
}

using Headers = map<string, string>;
using Parameters = map<string, string>;

atomic<int> numRetriesG{0};

WebClient BuildUploadRequest(const Config& config, const string& path,
                             int partNum, const string& uploadId) {
    Parameters params = {{"partNumber", to_string(partNum + 1)},
                         {"uploadId", uploadId}};
    auto signedHeaders =
        SignHeaders(config.s3AccessKey, config.s3SecretKey, config.endpoint,
                    "PUT", config.bucket, config.key, "", params);
    Headers headers(begin(signedHeaders), end(signedHeaders));
    WebClient req(config.endpoint, path, "PUT", params, headers);
    return req;
}

string BuildEndUploadXML(vector<future<string>>& etags) {
    string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<CompleteMultipartUpload "
        "xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">\n";
    for (int i = 0; i != etags.size(); ++i) {
        if (!etags[i].valid()) {
            throw runtime_error("Error - request " + to_string(i));
            return "";
        }
        const string part = "<Part><ETag>" + etags[i].get() +
                            "</ETag><PartNumber>" + to_string(i + 1) +
                            "</PartNumber></Part>";
        xml += part;
    }
    xml += "</CompleteMultipartUpload>";
    return xml;
}

WebClient BuildEndUploadRequest(const Config& config, const string& path,
                                vector<future<string>>& etags,
                                const string& uploadId) {
    Parameters params = {{"uploadId", uploadId}};
    auto signedHeaders =
        SignHeaders(config.s3AccessKey, config.s3SecretKey, config.endpoint,
                    "POST", config.bucket, config.key, "", params);
    Headers headers(begin(signedHeaders), end(signedHeaders));
    WebClient req(config.endpoint, path, "POST", params, headers);
    req.SetMethod("POST");
    req.SetPostData(BuildEndUploadXML(etags));
    return req;
}

string UploadPart(const Config& config, const string& path,
                  const string& uploadId, int i, size_t offset,
                  size_t chunkSize, int maxTries = 1, int tryNum = 1) {
    WebClient ul = BuildUploadRequest(config, path, i, uploadId);
    const bool ok = ul.UploadFile(config.file, offset, chunkSize);
    if (!ok) {
        throw(runtime_error("Cannot upload chunk " + to_string(i + 1)));
    }
    const string etag = HTTPHeader(ul.GetHeaderText(), "[Ee][Tt]ag");
    if (etag.empty()) {
        if (tryNum == maxTries) {
            throw(runtime_error("No ETag found in HTTP header"));
        } else {
            numRetriesG += 1;
            return UploadPart(config, path, uploadId, i, offset, chunkSize,
                              maxTries, ++tryNum);
        }
    }
    return etag;
}

string UploadPartMem(const char* src, const Config& config, const string& path,
                     const string& uploadId, int i, size_t offset,
                     size_t chunkSize, int maxTries = 1, int tryNum = 1) {
    WebClient ul = BuildUploadRequest(config, path, i, uploadId);
    const bool ok = ul.UploadDataFromBuffer(src, offset, chunkSize);
    if (!ok) {
        throw(runtime_error("Cannot upload chunk " + to_string(i + 1) + " " +
                            ul.ErrorMsg()));
    }
    const string etag = HTTPHeader(ul.GetHeaderText(), "[Ee][Tt]ag");
    if (etag.empty()) {
        if (tryNum == maxTries) {
            throw(runtime_error("No ETag found in HTTP header"));
        } else {
            numRetriesG += 1;
            return UploadPartMem(src, config, path, uploadId, i, offset,
                                 chunkSize, maxTries, ++tryNum);
        }
    }
    return etag;
}

void InitConfig(Config& config) {
    if (!config.s3AccessKey.empty() && !config.s3SecretKey.empty()) return;
    const string fname = config.credentials.empty()
                             ? GetHomeDir() + "/.aws/credentials"
                             : config.credentials;
    config.awsProfile =
        config.awsProfile.empty() ? "default" : config.awsProfile;
    Toml toml = ParseTomlFile(fname);  // only default profile supported
    if (toml.find(config.awsProfile) == toml.end()) {
        throw invalid_argument("ERROR: profile " + config.awsProfile +
                               " not found");
    }
    config.s3AccessKey = toml[config.awsProfile]["aws_access_key_id"];
    config.s3SecretKey = toml[config.awsProfile]["aws_secret_access_key"];
}

void LoadData(const char* fname, char* dest, size_t offset, size_t size) {
    FILE* f = fopen(fname, "rb");
    if (!f) {
        throw runtime_error("Cannot open input file for reading");
    }
    if (fseek(f, offset, SEEK_SET)) {
        throw runtime_error("Cannot move file pointer");
    }
    if (fread(dest, size, 1, f) != 1) {
        throw runtime_error("Error reading input file");
    }
    if (fclose(f)) {
        throw runtime_error("Error closing input file after read operation");
    }
}

//------------------------------------------------------------------------------
int main(int argc, char const* argv[]) {
    try {
        Config config;
        auto cli =
            lyra::help(config.showHelp)
                .description("Upload file to S3 bucket") |
            lyra::opt(config.s3AccessKey,
                      "awsAccessKey")["-a"]["--access_key"]("AWS access key")
                .optional() |
            lyra::opt(config.s3SecretKey,
                      "awsSecretKey")["-s"]["--secret_key"]("AWS secret key")
                .optional() |
            lyra::opt(config.endpoint,
                      "endpoint")["-e"]["--endpoint"]("Endpoing URL")
                .required() |
            lyra::opt(config.bucket, "bucket")["-b"]["--bucket"]("Bucket name")
                .required() |
            lyra::opt(config.key, "key")["-k"]["--key"]("Key name").required() |
            lyra::opt(config.file, "file")["-f"]["--file"]("File name")
                .required() |
            lyra::opt(config.jobs, "parallel jobs")["-j"]["--jobs"](
                "Number inputFile parallel jobs")
                .optional() |
            lyra::opt(config.credentials,
                      "credentials file")["-c"]["--credentials"](
                "Credentials file, AWS cli format")
                .optional() |
            lyra::opt(config.awsProfile,
                      "AWS config profile")["-p"]["--profile"](
                "Profile in AWS config file")
                .optional() |
            lyra::opt(config.maxRetries, "Max retries")["-r"]["--retries"](
                "Max number of per-multipart part retries")
                .optional() |
            lyra::opt(config.memoryMapping,
                      "Configure memory mapping options")["-m"]["--mmap"](
                "memory mapping: 'none', 'map', 'preload'")
                .choices("none", "preload", "map")
                .optional();

        InitConfig(config);

        // Parse the program arguments:
        auto result = cli.parse({argc, argv});

        if (!result) {
            cerr << result.errorMessage() << endl;
            cerr << cli << endl;
            exit(1);
        }
        if (config.showHelp) {
            cout << cli;
            return 0;
        }
        Validate(config);
        FILE* inputFile = fopen(config.file.c_str(), "rb");
        if (!inputFile) {
            throw runtime_error(string("cannot open file ") + config.file);
        }
        fclose(inputFile);
        string path = "/" + config.bucket + "/" + config.key;

        if (config.jobs > 1) {
            // retrieve file size
            const size_t fileSize = FileSize(config.file);
            // compute chunk size
            const size_t chunkSize = fileSize / config.jobs;
            // compute last chunk size
            const size_t lastChunkSize = fileSize % config.jobs == 0
                                             ? chunkSize
                                             : fileSize % config.jobs;
            // initiate request
            auto signedHeaders = SignHeaders(
                config.s3AccessKey, config.s3SecretKey, config.endpoint, "POST",
                config.bucket, config.key, "", {{"uploads=", ""}});
            map<string, string> headers(begin(signedHeaders),
                                        end(signedHeaders));
            WebClient req(config.endpoint, path, "POST", {{"uploads=", ""}},
                          headers);
            if (!req.Send()) {
                throw runtime_error("Error sending request: " + req.ErrorMsg());
            }
            if (req.StatusCode() >= 400) {
                const string errcode = XMLTag(req.GetContentText(), "[Cc]ode");
                throw runtime_error("Error sending begin upload request - " +
                                    errcode);
            }
            vector<uint8_t> resp = req.GetContent();
            const string xml(begin(resp), end(resp));
            const string uploadId = XMLTag(xml, "[Uu]pload[Ii][dD]");
            vector<future<string>> etags(config.jobs);
            int fin = -1;
            char* src = nullptr;
            vector<char> preloadBuffer;  // in case of pre-load
            if (config.memoryMapping == "map") {
                fin = open(config.file.c_str(), O_RDONLY | O_LARGEFILE);
                if (fin < 0) throw runtime_error("Cannot open input file");
                src =
                    (char*)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fin, 0);
                if (src == MAP_FAILED) runtime_error("Cannot map input file");
                for (int i = 0; i != config.jobs; ++i) {
                    const size_t sz =
                        i != config.jobs - 1 ? chunkSize : lastChunkSize;
                    etags[i] = async(launch::async, UploadPartMem, src, config,
                                     path, uploadId, i, chunkSize * i, sz,
                                     config.maxRetries, 1);
                }
            } else if (config.memoryMapping == "preload") {
                const size_t fsize = FileSize(config.file);
                if (fsize <= 0) {
                    throw runtime_error("Error retrieving file size");
                }
                preloadBuffer.resize(fsize);
                FILE* f = fopen(config.file.c_str(), "rb");
                if (!f) {
                    throw runtime_error("Cannot open input file for reading");
                }
                vector<future<void>> loaders(config.jobs);
                auto start = chrono::high_resolution_clock::now();
                for (int j = 0; j != config.jobs; ++j) {
                    const size_t sz =
                        j != config.jobs - 1 ? chunkSize : lastChunkSize;
                    loaders[j] =
                        async(launch::async, LoadData, config.file.c_str(),
                              preloadBuffer.data(), chunkSize * j, sz);
                }
                for (auto& l : loaders) l.wait();
                auto end = chrono::high_resolution_clock::now();
                cout << "Read time: "
                     << chrono::duration_cast<chrono::milliseconds>(end - start)
                            .count()
                     << " ms" << endl;
                for (int i = 0; i != config.jobs; ++i) {
                    const size_t sz =
                        i != config.jobs - 1 ? chunkSize : lastChunkSize;
                    etags[i] =
                        async(launch::async, UploadPartMem,
                              &preloadBuffer[0] + chunkSize * i, config, path,
                              uploadId, i, 0, sz, config.maxRetries, 1);
                }
            } else if (config.memoryMapping == "none") {
                for (int i = 0; i != config.jobs; ++i) {
                    const size_t sz =
                        i != config.jobs - 1 ? chunkSize : lastChunkSize;
                    etags[i] =
                        async(launch::async, UploadPart, config, path, uploadId,
                              i, chunkSize * i, sz, config.maxRetries, 1);
                }
            } else {
                throw invalid_argument("Wrong memory mapping option");
            }

            WebClient endUpload =
                BuildEndUploadRequest(config, path, etags, uploadId);
            if (config.memoryMapping == "mmap") {
                if (munmap(src, fileSize))
                    throw runtime_error("Cannot unmap output file");
                if (close(fin)) throw runtime_error("Error closing input file");
            }
            if (!endUpload.Send()) {
                throw runtime_error("Error sending request: " + req.ErrorMsg());
            }
            if (endUpload.StatusCode() >= 400) {
                const string errcode =
                    XMLTag(endUpload.GetContentText(), "[Cc]ode");
                throw runtime_error("Error sending end unpload request - " +
                                    errcode);
            }
            const string etag =
                XMLTag(endUpload.GetContentText(), "[Ee][Tt]ag");
            if (etag.empty()) {
                cerr << "Error sending end upload request" << endl;
            }
            cout << etag << endl;
        } else {
            auto signedHeaders = SignHeaders(
                config.s3AccessKey, config.s3SecretKey, config.endpoint, "PUT",
                config.bucket, config.key, "");
            map<string, string> headers(begin(signedHeaders),
                                        end(signedHeaders));
            WebClient req(config.endpoint, path, "PUT", {}, headers);
            if (!req.UploadFile(config.file)) {
                throw runtime_error("Error sending request: " + req.ErrorMsg());
            }
            if (req.StatusCode() >= 400) {
                const string errcode = XMLTag(req.GetContentText(), "[Cc]ode");
                throw runtime_error("Error sending end unpload request - " +
                                    errcode);
            }
            const string etag = HTTPHeader(req.GetHeaderText(), "[Ee][Tt]ag");
            if (etag[0] == '"') {
                cout << etag.substr(1, etag.size() - 2);
            } else {
                cout << etag << endl;
            }
            if (etag.empty()) {
                throw runtime_error("Error sending upload request");
            }
        }
        if (numRetriesG > 0) cout << "Num retries: " << numRetriesG << endl;
        return 0;
    } catch (const exception& e) {
        cerr << e.what() << endl;
        return 1;
    }
}