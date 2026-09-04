/* C:B**************************************************************************
This software is © 2014 Bright Plaza Inc. <drivetrust@drivetrust.com>

This file is part of sedutil.

sedutil is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

sedutil is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with sedutil.  If not, see <http://www.gnu.org/licenses/>.

 * C:E********************************************************************** */

// This routine should be compiled with the main routine sedutil-cli and the
// appropriate Common/Customizations folder, and not with the sedutil library.

#include "os.h"
#include "log.h"
#include "DtaOptions.h"
#include "DtaLexicon.h"
#include "DtaUsage.h"
#include "Version.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>

#if !defined(_WIN32)
#include <unistd.h>
#include <fcntl.h>
#include "GetPassPhrase.h"
#endif


void DtaWipe(void * buffer, size_t length)
{
#if defined(_WIN32)
  // No explicit_bzero here; a volatile store is the portable equivalent.
  volatile unsigned char * p = static_cast<volatile unsigned char *>(buffer);
  while (length--) *p++ = 0;
#else
  explicit_bzero(buffer, length);
#endif
}


#if !defined(_WIN32)

/** Read one line -- the password -- from an already-open file descriptor.
 *
 * One byte at a time, deliberately.  Everything after the first newline
 * belongs to whoever reads next, which is what lets
 * `--setSIDPassword fd:0 fd:0' take two consecutive lines from one stream.
 * A buffered read would swallow the second one.
 */
static uint8_t readPasswordFromFd(const char * what, int fd,
                                  char * out, size_t outsize)
{
  size_t length = 0;

  for (;;) {
    char c;
    const ssize_t n = read(fd, &c, 1);
    if (n < 0) {
      if (EINTR == errno) continue;
      LOG(E) << "Cannot read the " << what << ": " << strerror(errno);
      return DTAERROR_INVALID_COMMAND;
    }
    if (0 == n)    break;             // source ended without a newline
    if ('\n' == c) break;             // one line is one password
    if ('\0' == c) {
      // The password reaches DtaHashPwd() as a C string and would be
      // truncated here without a word.
      LOG(E) << "The " << what << " contains a NUL byte";
      return DTAERROR_INVALID_COMMAND;
    }
    if (length + 1 >= outsize) {
      LOG(E) << "The " << what << " is longer than "
             << (unsigned int)(outsize - 1) << " characters";
      return DTAERROR_INVALID_COMMAND;
    }
    out[length++] = c;
  }

  if (0 < length && '\r' == out[length - 1])  // so CRLF files work
    length--;

  out[length] = '\0';
  return DTAERROR_SUCCESS;
}

/** Apply the same one-line rule to a password already in memory. */
static uint8_t copyPasswordLine(const char * what, const char * value,
                                char * out, size_t outsize)
{
  size_t length = 0;

  while ('\0' != value[length] && '\n' != value[length]) {
    if (length + 1 >= outsize) {
      LOG(E) << "The " << what << " is longer than "
             << (unsigned int)(outsize - 1) << " characters";
      return DTAERROR_INVALID_COMMAND;
    }
    out[length] = value[length];
    length++;
  }

  if (0 < length && '\r' == out[length - 1])
    length--;

  out[length] = '\0';
  return DTAERROR_SUCCESS;
}

/** Resolve one -f source specification into the password it names.
 *
 * The bare form is a path, so the maintainer's "-f just reinterprets the
 * argument as a file name" reading is the default; the prefixes are additions
 * to it.  A file actually named "prompt" or "-" is reachable as "file:prompt".
 */
static uint8_t readPasswordFromSource(const char * what, const char * spec,
                                      char * out, size_t outsize)
{
  // "-" and "fd:N" -- an inherited file descriptor
  int fd = -1;
  if (0 == strcmp("-", spec)) {
    fd = STDIN_FILENO;
  } else if (0 == strncmp("fd:", spec, 3)) {
    char * end = NULL;
    const long n = strtol(spec + 3, &end, 10);
    if (end == spec + 3 || '\0' != *end || n < 0 || n > INT_MAX) {
      LOG(E) << "Not a file descriptor number: \"" << spec << "\"";
      return DTAERROR_INVALID_COMMAND;
    }
    fd = (int)n;
  }
  if (0 <= fd)
    return readPasswordFromFd(what, fd, out, outsize);

  // "env:NAME" -- a named environment variable.  Unset is an error; set to
  // the empty string is the empty password.
  if (0 == strncmp("env:", spec, 4)) {
    const char * const name = spec + 4;
    const char * const value = getenv(name);
    if (NULL == value) {
      LOG(E) << "Environment variable " << name << " is not set";
      return DTAERROR_INVALID_COMMAND;
    }
    return copyPasswordLine(what, value, out, outsize);
  }

  // "prompt" -- the controlling terminal, echo off.  Not stdin: `-f prompt'
  // has to keep working when stdin is a pipe.
  if (0 == strcmp("prompt", spec)) {
    FILE * const tty = fopen("/dev/tty", "r+");
    if (NULL == tty) {
      LOG(E) << "Cannot open /dev/tty to ask for the " << what << ": " << strerror(errno);
      return DTAERROR_INVALID_COMMAND;
    }
    // Unbuffered, so a read stops at the end of the line instead of pulling
    // the rest of the terminal input into a buffer we are about to close.
    // That is what lets "-f --setSIDPassword prompt prompt" ask twice.
    setvbuf(tty, NULL, _IONBF, 0);
    const std::string prompt = std::string("Enter the ") + what + ": ";
    std::string phrase = GetPassPhrase(prompt.c_str(), false, tty);
    fclose(tty);
    const uint8_t result = copyPasswordLine(what, phrase.c_str(), out, outsize);
    if (!phrase.empty())
      DtaWipe(&phrase[0], phrase.size());
    return result;
  }

  // "file:PATH", or a bare path
  const char * const path = (0 == strncmp("file:", spec, 5)) ? spec + 5 : spec;
  const int opened = open(path, O_RDONLY);
  if (opened < 0) {
    LOG(E) << "Cannot open " << path << ": " << strerror(errno);
    return DTAERROR_INVALID_COMMAND;
  }
  const uint8_t result = readPasswordFromFd(what, opened, out, outsize);
  close(opened);
  return result;
}

#endif // !_WIN32


/** Turn one password argument into the password itself.
 *
 * Without -f the argument is the password.  With -f it names a source and the
 * password is one line read from that source.  Either way opts->*_data holds
 * the secret afterwards, and argv is never dereferenced for it again.
 */
static uint8_t resolvePassword(int argc, char * argv[], const char * what,
                               uint8_t index, bool fromSource,
                               char * out, size_t outsize)
{
  out[0] = '\0';

  if (0 == index || index >= argc)
    return DTAERROR_SUCCESS;    // this action takes no such password

  const char * const argument = argv[index];

  if (!fromSource) {
    if (strlen(argument) >= outsize) {
      LOG(E) << "The " << what << " is longer than "
             << (unsigned int)(outsize - 1) << " characters";
      return DTAERROR_INVALID_COMMAND;
    }
    strcpy(out, argument);
    return DTAERROR_SUCCESS;
  }

#if defined(_WIN32)
  (void)argument;
  LOG(E) << "-f is not supported on this platform";
  return DTAERROR_INVALID_COMMAND;
#else
  LOG(D1) << "Reading the " << what << " from " << argument;
  const uint8_t result = readPasswordFromSource(what, argument, out, outsize);
  if (DTAERROR_SUCCESS != result)
    DtaWipe(out, outsize);   // do not leave half a secret behind on the way out
  return result;
#endif
}


#define LOCKINGRANGEARG(lockingrange) \
TESTARG(0, lockingrange, 0)            \
TESTARG(1, lockingrange, 1)            \
TESTARG(2, lockingrange, 2)            \
TESTARG(3, lockingrange, 3)            \
TESTARG(4, lockingrange, 4)            \
TESTARG(5, lockingrange, 5)            \
TESTARG(6, lockingrange, 6)            \
TESTARG(7, lockingrange, 7)            \
TESTARG(8, lockingrange, 8)            \
TESTARG(9, lockingrange, 9)            \
TESTARG(10, lockingrange, 10)          \
TESTARG(11, lockingrange, 11)          \
TESTARG(12, lockingrange, 12)          \
TESTARG(13, lockingrange, 13)          \
TESTARG(14, lockingrange, 14)          \
TESTARG(15, lockingrange, 15)          \
TESTFAIL("Invalid Locking Range (0-15)")

#define MBRSTATEARG(arg,mbrstate) \
TESTARG(ON, mbrstate, 1)      \
TESTARG(on, mbrstate, 1)      \
TESTARG(Y, mbrstate, 1)       \
TESTARG(y, mbrstate, 1)       \
TESTARG(n, mbrstate, 0)       \
TESTARG(N, mbrstate, 0)       \
TESTARG(off, mbrstate, 0)     \
TESTARG(OFF, mbrstate, 0)     \
TESTFAIL("Invalid " #arg "argument not <ON|on|Y|y|OFF|off|N|n>")

#define LOCKINGSTATEARG(lockingstate) \
TESTARG(RW, lockingstate, OPAL_LOCKINGSTATE::READWRITE) \
TESTARG(rw, lockingstate, OPAL_LOCKINGSTATE::READWRITE) \
TESTARG(RO, lockingstate, OPAL_LOCKINGSTATE::READONLY)  \
TESTARG(ro, lockingstate, OPAL_LOCKINGSTATE::READONLY)  \
TESTARG(LK, lockingstate, OPAL_LOCKINGSTATE::LOCKED)    \
TESTARG(lk, lockingstate, OPAL_LOCKINGSTATE::LOCKED)    \
TESTFAIL("Invalid locking state <RW|rw|RO|ro|LK|lk>")

#define TCGRESETTYPEARG(resettype) \
TESTARG(0, resettype, 0) \
TESTARG(1, resettype, 1) \
TESTARG(2, resettype, 2) \
TESTARG(3, resettype, 3) \
TESTFAIL("Invalid TCGreset argument not <0|1|2|3>")

uint8_t DtaOptions(int argc, char * argv[], DTA_OPTIONS * opts)
{
    memset(opts, 0, sizeof (DTA_OPTIONS));
    opts->output_format = DEFAULT_OUTPUT_FORMAT;
    uint16_t loggingLevel = DEFAULT_LOGGING_LEVEL;
    uint8_t baseOptions = 2; // program and option
    CLogLevel = CLog::FromInt(loggingLevel);
    RCLogLevel = RCLog::FromInt(loggingLevel);
    if (2 > argc) {
        usage();
        return DTAERROR_INVALID_COMMAND;
    }
    for (uint8_t i = 1; i < argc; i++) {
        if (!(strcmp("-h", argv[i])) || !(strcmp("--help", argv[i]))) {
            usage();
            return DTAERROR_INVALID_COMMAND;
        }

        if ('v' == argv[i][1]) {
            // logging level set to length of any arg staring with 'v'
          baseOptions += 1;
          loggingLevel += (uint16_t)(strlen(argv[i]) - 1);
          if (loggingLevel > MAX_LOGGING_LEVEL) loggingLevel = MAX_LOGGING_LEVEL;
          CLogLevel = CLog::FromInt(loggingLevel);
          RCLogLevel = RCLog::FromInt(loggingLevel);
          LOG(D) << "Log level set to " << CLog::ToString(CLog::FromInt(loggingLevel));
          LOG(D) << "sedutil version : " << GIT_VERSION;
        } else if (!(strcmp("-a", argv[i]))) {
          baseOptions += 1;
          opts->skip_activate = true;
          LOG(D) << "Do not activate LockingSP";
        } else if (!(strcmp("-u", argv[i]))) {
          baseOptions += 1;
          opts->usermode = true;
          LOG(D) << "user mode ON";
        } else if (!(strcmp("-t", argv[i]))) {
          baseOptions += 1;
          opts->translate_req = true;
          LOG(D) << "translate hashed string to data";
        } else if (!(strcmp("-n", argv[i]))) {
          baseOptions += 1;
          opts->no_hash_passwords = true;
          LOG(D) << "Password hashing is disabled";
        } else if (!(strcmp("-f", argv[i]))) {
          baseOptions += 1;
          opts->password_from_source = true;
          LOG(D) << "Password arguments name a source, not the password itself";
        } else if (!strcmp("-l", argv[i])) {
          baseOptions += 1;
          opts->output_format = sedutilNormal;
          outputFormat = sedutilNormal;
        } else if (!(('-' == argv[i][0]) && ('-' == argv[i][1])) &&
                                            (0 == opts->action)) {
          LOG(E) << "Argument " << (uint16_t) i << " (" << argv[i] << ") should be a command";
          return DTAERROR_INVALID_COMMAND;
        }

#include "DtaOptions.inc"

#include "Customizations/DtaExtensionOptions.inc"

        else {
            LOG(E) << "Invalid command line argument " << argv[i];
			return DTAERROR_INVALID_COMMAND;
        }
    }

    // Now that the argument loop has settled the argv indices, turn the
    // password arguments into the passwords themselves, once.  Order matters:
    // the password precedes the new password in argv for every action, so
    // "--setSIDPassword fd:0 fd:0" reads them in that order off one stream.
    uint8_t result;

    result = resolvePassword(argc, argv, "password",
                             opts->password, opts->password_from_source,
                             opts->password_data, sizeof(opts->password_data));
    if (DTAERROR_SUCCESS != result)
        return result;

    result = resolvePassword(argc, argv, "new password",
                             opts->newpassword, opts->password_from_source,
                             opts->newpassword_data, sizeof(opts->newpassword_data));
    if (DTAERROR_SUCCESS != result)
        return result;

    return DTAERROR_SUCCESS;
}
