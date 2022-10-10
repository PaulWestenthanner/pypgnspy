/*
 *  This file is part of uci-analyser: a UCI-based Chess Game Analyser
 *  Copyright (C) 2013-2017 David J. Barnes
 *
 *  uci-analyser is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  uci-analyser is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with uci-analyser.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  David J. Barnes may be contacted as d.j.barnes@kent.ac.uk
 *  https://www.cs.kent.ac.uk/people/staff/djb/
 */

#include "engine.h"
#include <sstream>
#include <string.h>
#include <stdlib.h>

#include "utils.h"

/*
 * Handle interactions with a UCI-compatible engine.
 */

void Engine::go(void) {
    stringstream ss;
    ss << "go depth " << searchDepth;
    send(ss.str());
}

void Engine::setPosition(const string& moves, const string& fenstring) {
    if(fenstring.length() == 0) {
        send("position startpos moves " + moves);
    }
    else {
        setFENPosition(fenstring, moves);
    }
}

void Engine::setFENPosition(const string& fenstring, const string& moves) {
    stringstream ss;
    ss << "position fen " << fenstring << " moves " + moves;
    send(ss.str());
}

/*
 * Send a setoption command to the engine using the
 * given name and value.
 */
void Engine::setOption(const string& name, const string& value) {
    stringstream ss;
    ss << "setoption name " << name << " value " << value;
    send(ss.str());
}

/*
 * Send a setoption command to the engine using the
 * given name and value.
 */
void Engine::setOption(const string& name, int value) {
    stringstream ss;
    ss << value;
    setOption(name, ss.str());
}

void Engine::setOptions(map<string, string>& options) {
    map<string, string>::iterator it;
    for (it = options.begin(); it != options.end(); it++) {
        setOption(it->first, it->second);
    }
}

/*
 * Search with the given moves.
 */
void Engine::searchMoves(const string& moves) {
    stringstream ss;
    ss << "go depth " << searchDepth << " searchmoves " + moves;
    send(ss.str());
}

/*
 * Initialise the UCI engine.
 * Return true if intialised ok; false otherwise.
 */
bool Engine::initEngine(int variations, int searchDepth,
        map<string, string>& options) {
    this->variations = variations;
    this->searchDepth = searchDepth;

    send("uci");
    if(!setIdentity()) {
        cerr << "Failed to identify the engine." << endl;
        cerr << "No \"id name\" found." << endl;
        return false;
    }
    else if (waitForResponse("uciok")) {
        // Set default options.
        setOption("UCI_AnalyseMode", "true");
        setOption("MultiPV", variations);

        // Set command-line options.
        setOptions(options);

        startNewGame();
        send("isready");
        return waitForResponse("readyok");
    } else {
        return false;
    }
}

/*
 * Check that the engine is ready.
 */
bool Engine::checkIsReady(void) {
    send("isready");

    bool eof = false;
    string response = getResponse(eof);
    return !eof && response.compare("readyok") == 0;
}

void Engine::quitEngine(void) {
    send("quit");
}

/*
 * Send the given string to the engine.
 */
void Engine::send(const string &str) {
    send(str.c_str());
}

/*
 * Send the given string to the engine.
 */
void Engine::send(const char *str) {
        //cout << "# Send: " << str << endl;
#ifdef __unix__
    fprintf(toEngine, "%s\n", str);
    fflush(toEngine);
#else
    DWORD dwWritten;
    BOOL bSuccess = FALSE;

    bSuccess = WriteFile(writeToEngine, str, strlen(str), &dwWritten, NULL);
    if (bSuccess) {
        static const char *newl = "\n";
        bSuccess |= WriteFile(writeToEngine, newl, strlen(newl), &dwWritten, NULL);
    }
#endif
}

/*
 * Wait for the given response from the engine.
 * Return true on success or false on failure (EOF).
 */
bool Engine::waitForResponse(const char *str) {
    bool eof = false;
    string response;
    do {
        response = getResponse(eof);
    } while (strcmp(str, response.c_str()) != 0 && !eof);
    return strcmp(str, response.c_str()) == 0;
}

/*
 * Read and return a single line of response from the engine.
 * Set eof if the end of file is reached.
 */
string Engine::getResponse(bool& eof) {
    const int MAXBUFF = 100;
    // Since the reads are not guaranteed to be line-based, buffer retains
    // text read but not returned on a previous call.
    static char buffer[MAXBUFF + 1] = "";
    // What is returned.
    string result;
    bool endOfLine = false;
    //cout << "# get response" << endl;
    eof = false;
    while (!endOfLine && !eof) {
        if (*buffer == '\0') {
            // Nothing left from the previous read.
#if __unix__
            char *readResult = fgets(buffer, MAXBUFF, fromEngine);
            if (readResult == NULL) {
                    eof = true;
            }
#else
            DWORD bytesRead;
            DWORD success = ReadFile(readFromEngine, buffer, MAXBUFF, &bytesRead, NULL);
            buffer[bytesRead] = '\0';
            eof = !success || bytesRead == 0;
#endif
            }
            if (!eof) {
                // Look for the end of the line, which might not have been read.
                // NB: There is a boundary error possible here, where a \n\r combination
                // is split across two reads. If that happens then the second char will be
                // treated as a spurious blank line.
                int index = 0;
                char ch = buffer[index];
                while (ch != '\0' && ch != '\n' && ch != '\r') {
                    index++;
                    ch = buffer[index];
                }
                if (ch == '\n' || ch == '\r') {
                    endOfLine = true;
                    buffer[index] = '\0';
                    index++;
                    char nextChar = buffer[index];
                    if (nextChar == '\n' || nextChar == '\r') {
                            index++;
                    }
                }
                // Concatenate up to the end of line, or everything if not
                // at the end of the line.
                result += buffer;
                // Retain any left.
                if (endOfLine) {
#ifdef __unix__
                    strcpy(buffer, &buffer[index]);
#else
                    strcpy_s(buffer, &buffer[index]);
#endif
                    }
                    else {
                        *buffer = '\0';
                    }
                }
        }
        if (!eof) {
            //cout << "# [" << result << "]" << endl;
        }
        return result;
}

/* Look for "id name" in the engine's initial output and use it
 * to set the engine's identity.
 */
bool Engine::setIdentity(void)
{
   // Get the identity.
    const char *id_prefix = "id name ";
    bool eof, identitySet = false;

    do {
        string idResponse = getResponse(eof);
        if (!eof) {
            if(idResponse.find(id_prefix) == 0) {
                this->identity = idResponse.substr(strlen(id_prefix));
                identitySet = true;
            }
        }
    } while (!identitySet && !eof);
    return identitySet;
}

#ifdef __unix__
// The unix part of the startEngine method is a version of the following:
// https://stackoverflow.com/questions/478898/how-to-execute-a-command-and-get-output-of-command-within-c
// This code is not necessarily covered by the GPL notice.

// The low-level file descriptor numbers.
#define READFD 0
#define WRITEFD 1
#endif

/*
 * Start the given engine.
 */
bool Engine::startEngine(const string& engineName) {
#if __unix__
    int parentToChild[2];
    int childToParent[2];
    string dataReadFromChild;

    ASSERT_IS(0, pipe(parentToChild));
    ASSERT_IS(0, pipe(childToParent));

    switch (enginePID = fork()) {
        case -1:
            FAIL("Fork failed");
            return false;

        case 0: /* Child */
            ASSERT_NOT(-1, dup2(parentToChild[ READFD ], STDIN_FILENO));
            ASSERT_NOT(-1, dup2(childToParent[ WRITEFD ], STDOUT_FILENO));
            //ASSERT_NOT(-1, dup2( childToParent[ WRITEFD ], STDERR_FILENO ) );
            ASSERT_IS(0, close(parentToChild [ WRITEFD ]));
            ASSERT_IS(0, close(childToParent [ READFD ]));

            execlp(engineName.c_str(), engineName.c_str(), (char *) NULL);

            cerr << "Failed to start the engine: " << engineName << endl;
            close(parentToChild[READFD]);
            close(childToParent[WRITEFD]);
            exit(-1);
            return false;

        default: /* Parent */
            ASSERT_IS(0, close(parentToChild [ READFD ]));
            ASSERT_IS(0, close(childToParent [ WRITEFD ]));

            // Set up FILE * wrappers for the file descriptors.
            fromEngine = fdopen(childToParent[READFD], "r");
            toEngine = fdopen(parentToChild[WRITEFD], "w");

            ASSERT_NOT(NULL, fromEngine);
            ASSERT_NOT(NULL, toEngine);
            return true;
    }
#else
    /* The windows version of startEngine is heavily based on the code found in the article,
     * "Creating a Child Process with Redirected Input and Output"
     * at http://msdn.microsoft.com/en-us/library/windows/desktop/ms682499%28v=vs.85%29.aspx
     * 
     * This code is not covered by the GPL notice.
     */
    SECURITY_ATTRIBUTES saAttr;

    // Set the bInheritHandle flag so pipe handles are inherited. 

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Create a pipe for the child process's STDOUT. 
    HANDLE unusedChildStdinRead, unusedChildStdoutWrite;

    if (!CreatePipe(&readFromEngine, &unusedChildStdoutWrite, &saAttr, 0)) {
        cerr << "Failed to create the pipe for reading from the engine." << endl;
        return false;
    }

    // Ensure the read handle to the pipe for STDOUT is not inherited.

    if (!SetHandleInformation(readFromEngine, HANDLE_FLAG_INHERIT, 0)) {
        cerr << "Failed to set up the file handle to read from the engine." << endl;
        return false;
    }

    // Create a pipe for the child process's STDIN. 

    if (!CreatePipe(&unusedChildStdinRead, &writeToEngine, &saAttr, 0)) {
        cerr << "Failed to set up the file handle to write to the engine." << endl;
        return false;
    }

    // Ensure the write handle to the pipe for STDIN is not inherited. 

    if (!SetHandleInformation(writeToEngine, HANDLE_FLAG_INHERIT, 0)){
        cerr << "Failed to create the pipe for writing to the engine." << endl;
        return false;
    }

    // Create the child process. 
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    BOOL bSuccess = FALSE;

    // Set up members of the PROCESS_INFORMATION structure. 

    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    // Set up members of the STARTUPINFO structure. 
    // This structure specifies the STDIN and STDOUT handles for redirection.

    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = unusedChildStdoutWrite;
    siStartInfo.hStdOutput = unusedChildStdoutWrite;
    siStartInfo.hStdInput = unusedChildStdinRead;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    // Create the child process. 
    CA2T commandLine(engineName.c_str());
    bSuccess = CreateProcess(NULL,
            commandLine,     // command line 
            NULL,          // process security attributes 
            NULL,          // primary thread security attributes 
            TRUE,          // handles are inherited 
            0,             // creation flags 
            NULL,          // use parent's environment 
            NULL,          // use parent's current directory 
            &siStartInfo,  // STARTUPINFO pointer 
            &piProcInfo);  // receives PROCESS_INFORMATION 

    // If an error occurs, exit the application. 
    if (!bSuccess) {
        cerr << "Failed to create the process: " << engineName << endl;
        return false;
    }
    else
    {
        // Close handles to the child process and its primary thread.
        // Some applications might keep these handles to monitor the status
        // of the child process, for example. 

        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);
        return true;
    }
#endif
}
