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

//create file with desired stripe size on 

#include <lustre/lustreapi.h>

#include <cerrno>
#include <iostream>

using namespace std;
//important: make sure the file does not exist already
int main(int argc, char *argv[]) {
    if (argc != 4) {
        cerr << "usage: " << argv[0]
             << " <filename> <stripe size> <number of OSTs>" << endl;
        exit(EXIT_FAILURE);
    }
    const int rc = llapi_file_create(argv[1], atoll(argv[2]), 0, atoll(argv[3]),
                                     LOV_PATTERN_RAID0);     
    if (rc < 0) {
        cerr << "file creation has failed, error: " << strerror(-rc);
        exit(EXIT_FAILURE);
    }
    cout << argv[1] << " with stripe size " << atoll(argv[2])
         << " striped across " << atoll(argv[3]) << " OSTs, has been created!"
         << endl;
    return 0;
}