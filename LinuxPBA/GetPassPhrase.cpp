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

#include "os.h"
#include "GetPassPhrase.h"
#include <string>
#include <termios.h>
#include <stdio.h>
using namespace std;

static struct termios tiosold, tiosnew;
/* Initialize new terminal i/o settings */
void initTermios(int fd, int echo)
{
  tcgetattr(fd, &tiosold); /* grab old terminal i/o settings */
  tiosnew = tiosold; /* make new settings same as old settings */
  tiosnew.c_lflag &= ~ICANON; /* disable buffered i/o */
  if (echo)
    tiosnew.c_lflag |= ECHO;
  else
    tiosnew.c_lflag &= ~ECHO;
  tcsetattr(fd, TCSANOW, &tiosnew); /* use these new terminal i/o settings now */
}

/* Restore old terminal i/o settings */
void resetTermios(int fd)
{
  tcsetattr(fd, TCSANOW, &tiosold);
}

/* Read 1 character - echo defines echo mode.  Returns EOF at end of input. */
int getch_(FILE *in, int echo)
{
  initTermios(fileno(in), echo);
  int ch = fgetc(in);
  resetTermios(fileno(in));
  return ch;
}

string GetPassPhrase(const char *prompt, bool show_asterisk, FILE *console)
{
  const int BACKSPACE=127;
  const int LINEFEED=10;
  const int CARRIAGERETURN=13;
  const int ESCAPE=27;

  /* The PBA prompts on the standard streams.  sedutil-cli hands us an open
   * /dev/tty instead, so that `-f prompt' keeps working with a redirected
   * stdin -- reading fd 0 there would eat the piped data, not the user. */
  FILE * in  = (console != NULL) ? console : stdin;
  FILE * out = (console != NULL) ? console : stdout;

  string password;
  LOG(D4) << "Enter GetPassPhrase" << endl;
  fprintf(out, "\n\n%s", prompt);
  fflush(out);

  for (;;)
    {
      int ch = getch_(in, 0);
      /* End of input is end of the phrase, not a reason to spin forever
         reading EOF -- which is what a bare getchar() loop used to do. */
      if (ch == EOF || ch == LINEFEED || ch == CARRIAGERETURN)
        break;
      if (ch == BACKSPACE)
        {
          if (password.length() != 0)
            {
              if (show_asterisk)
                fprintf(out, "\b \b");
              password.resize(password.length()-1);
            }
        }
      else if (ch != ESCAPE) // ignore 'escape' key
        {
          password += (char)ch;
          if (show_asterisk)
            fprintf(out, "*");
        }
      fflush(out);
    }

  fprintf(out, "\n");
  fflush(out);

  return password;
}
