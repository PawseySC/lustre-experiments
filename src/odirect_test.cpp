/*******************************************************************************
 * BSD 3-Clause License
 *
 * Copyright (c) 2020, Commonwealth Scientific and Industrial Research
 * Organisation (CSIRO) and The Pawsey Supercomputing Centre
 *
 * Author: Ugo Varetto
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

//simply try to write to file opened with O_DIRECT flag set

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

using namespace std;

int main(int argc, char** argv) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <output file name>" << endl;
        exit(EXIT_FAILURE);
    }
    const int flags = O_WRONLY | O_CREAT | O_DIRECT;
    const mode_t mode = 0644;  // user read/write, group read, all read
    int fd = open(argv[1], flags, mode);
    if (fd < 0) {
        cerr << "Failed to open file. Error: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    const char buffer[] = {'a', 'b', 'c', 'd'};
    if (pwrite(fd, buffer, sizeof(buffer) / sizeof(buffer[0]), 0) < 0) {
        cerr << "Failed to write to file. Error: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    cout << "O_DIRECT supported" << endl;
    close(fd);
    return 0;
}