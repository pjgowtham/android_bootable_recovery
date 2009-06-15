/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "edify/expr.h"
#include "updater.h"
#include "install.h"
#include "minzip/Zip.h"

// Where in the package we expect to find the edify script to execute.
// (Note it's "updateR-script", not the older "update-script".)
#define SCRIPT_NAME "META-INF/com/google/android/updater-script"

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "unexpected number of arguments (%d)\n", argc);
        return 1;
    }

    char* version = argv[1];
    if (version[0] != '1' || version[1] != '\0') {
        fprintf(stderr, "wrong updater binary API; expected 1, got %s\n",
                version);
        return 2;
    }

    // Set up the pipe for sending commands back to the parent process.

    int fd = atoi(argv[2]);
    FILE* cmd_pipe = fdopen(fd, "wb");
    setlinebuf(cmd_pipe);

    // Extract the script from the package.

    char* package_data = argv[3];
    ZipArchive za;
    int err;
    err = mzOpenZipArchive(package_data, &za);
    if (err != 0) {
        fprintf(stderr, "failed to open package %s: %s\n",
                package_data, strerror(err));
        return 3;
    }

    const ZipEntry* script_entry = mzFindZipEntry(&za, SCRIPT_NAME);
    if (script_entry == NULL) {
        fprintf(stderr, "failed to find %s in %s\n", SCRIPT_NAME, package_data);
        return 4;
    }

    char* script = malloc(script_entry->uncompLen+1);
    if (!mzReadZipEntry(&za, script_entry, script, script_entry->uncompLen)) {
        fprintf(stderr, "failed to read script from package\n");
        return 5;
    }
    script[script_entry->uncompLen] = '\0';

    // Configure edify's functions.

    RegisterBuiltins();
    RegisterInstallFunctions();
    FinishRegistration();

    // Parse the script.

    Expr* root;
    int error_count = 0;
    yy_scan_string(script);
    int error = yyparse(&root, &error_count);
    if (error != 0 || error_count > 0) {
        fprintf(stderr, "%d parse errors\n", error_count);
        return 6;
    }

    // Evaluate the parsed script.

    UpdaterInfo updater_info;
    updater_info.cmd_pipe = cmd_pipe;
    updater_info.package_zip = &za;

    char* result = Evaluate(&updater_info, root);
    if (result == NULL) {
        const char* errmsg = GetError();
        fprintf(stderr, "script aborted with error: %s\n",
                errmsg == NULL ? "(none)" : errmsg);
        ClearError();
        return 7;
    } else {
        fprintf(stderr, "script result was [%s]\n", result);
        free(result);
    }

    mzCloseZipArchive(&za);

    return 0;
}