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
#pragma once
#include <string>
#include <cstdio>
using namespace std;

/** Read a pass phrase from a terminal with echo disabled.
 *
 * @param prompt         written to the console before reading
 * @param show_asterisk  echo one '*' per character typed
 * @param console        terminal to prompt on and read from, or NULL for
 *                       stdout/stdin.  sedutil-cli passes an open /dev/tty so
 *                       that `-f prompt' still works when stdin is a pipe.
 *
 * Reading stops at the end of the line (LF or CR) or at end of input.
 */
string GetPassPhrase(const char *prompt, bool show_asterisk=true, FILE *console=NULL);
