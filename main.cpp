#include <csignal> // for exit message upon CTRL+C
#include <fcntl.h> // for open() system call
#include <iostream> // for basic i/o ops
#include <map> // for color map
#include <string> // for string ops
#include <sys/wait.h> // for wait() call
#include <unistd.h> // for fork(), exec(), etc.
#include <vector> // for storing string tokens

#define FILEFLAGS (O_CREAT | O_WRONLY | O_TRUNC | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define FILEFLAGS_APPEND (O_APPEND | O_WRONLY | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define RW_PERMS 0666

// map of all supported colors for 'color' option:
const std::map<std::string, std::string> COLORS = {
  {"red", "\x1b[0;31m"},
  {"bred", "\x1b[1;31m"},
  {"green", "\x1b[0;32m"},
  {"bgreen", "\x1b[1;32m"},
  {"yellow", "\x1b[0;33m"},
  {"byellow", "\x1b[01;33m"},
  {"blue", "\x1b[0;34m"},
  {"bblue", "\x1b[1;34m"},
  {"magenta", "\x1b[0;35m"},
  {"bmagenta", "\x1b[1;35m"},
  {"cyan", "\x1b[0;36m"},
  {"bcyan", "\x1b[1;36m"},
  {"reset", "\x1b[0m"}
};

// array of all error strings
const std::string ERROR_STR[] = {
  "child process could not be created.\n",
  "unexpected error during process execution.\n",
  "no such directory\n"
};

/* ---------- FUNCTION DECLARATIONS ---------- */
void run_cmd(const std::string& cmd, bool is_background);
std::vector<std::string> splitByPipe(const std::string& input);
std::vector<std::string> tokenize(const std::string& input);
void handle_cd(const std::string dir);
void handle_pwd();

void handle_color(std::string color);
void clear_screen();
void exitSignalHandler(int signal);
void print_error(const int err_code);
/* ---------- END FUNCTION DECLARATIONS ---------- */

int main() {
  // set custom signal handler for SIGINT:
  signal(SIGINT, exitSignalHandler);

  // store a duplicate of STDIN:
  const int ORIG_STDIN = dup(STDIN_FILENO);

  // store the status of background processes (unused but required for waitpid()):
  siginfo_t status;

  // continuously take user input:
  std::string input;
  bool is_background;
  while (1) {
    // have any child processes finished? if so, kill them:
    waitid(P_PID, 0, &status, WNOHANG);

    // replace STDIN with ORIG_STDIN in case we changed it in a previous command:
    dup2(ORIG_STDIN, STDIN_FILENO);

    printf("shell >> ");

    // get user input:
    getline(std::cin, input);

    // if user wants to exit, exit the program:
    if (input == "exit") return 0;

    // if user wants to clear the screen:
    else if (input == "clear" || input == "cls") {
      clear_screen();
      continue;
    }

    // if user wants to change directory:
    else if (input == "cd") { // if no argument provided
      handle_cd("");
    }
    else if (!input.rfind("cd ", 0)) {
      handle_cd(input.substr(3));
      continue;
    }

    // if user wants to print the current working directory:
    // NOTE: we ignore anything that comes after 'pwd' as the Unix shell does
    else if (input == "pwd" || !input.rfind("pwd ", 0)) {
      handle_pwd();
      continue;
    }

    // if user wants to change the shell's text color:
    else if (!input.rfind("color", 0)) {
      handle_color(input.substr(6));
      continue;
    }

    // parse the given input by pipes:
    std::vector<std::string> piped = splitByPipe(input);

    // is this a background process?
    is_background = (piped.back() == "&");
    piped.pop_back(); // remove the "background flag" from command list

    // pipe loop structure, piping each process's STDOUT to the next process's STDIN:
    pid_t childpid; // to keep track of child's pid
    int fd[2]; // for pipe file descriptors
    for (int i = 0; i < piped.size() - 1; i++) {
      pipe(fd);

      // start the child process:
      childpid = fork();

      // make sure the child process has been successfully created:
      if (childpid < 0) {
        print_error(0);
        break;
      }
      // if we're in the child process:
      else if (!childpid) {
        dup2(fd[1], STDOUT_FILENO); // redirect child's output through pipe
        run_cmd(piped[i], is_background);
      }
      // if we're in the parent process:
      else {
        // wait for the child process to finish:
        dup2(fd[0], STDIN_FILENO); // redirect parent's input through pipe
        close(fd[1]);
      }
    }

    // still one more command to take care of!
    dup2(fd[0], STDIN_FILENO);

    // create the last process and execute the last command:
    childpid = fork();
    if (childpid < 0) print_error(0);
    if (!childpid) run_cmd(piped.back(), is_background);
    else if (!is_background) wait(NULL);
    close(fd[1]); // important to close this so system doesn't wait on pipe to close!
  }

  return 0;
}

void run_cmd(const std::string& cmd, bool is_background) {
  // if this is a background process:
  if (is_background) setpgid(0, 0);

  // tokenize command:
  std::vector<std::string> tokenized = tokenize(cmd);

  int FFLAGS = FILEFLAGS; // FILEFLAGS if no append, FILEFLAGS | O_APPEND if user wants to append
  int ARGC = tokenized.size() + 1; // length of argument array. will be reduced by 2 if there's redirection

  // naive implementation: only check if second-to-last token is '>'. if so, redirect output:
  if (tokenized.size() > 1) {
    // if user wants to append instead of overwrite file:
    if (tokenized[tokenized.size()-2] == ">>") FFLAGS = FILEFLAGS_APPEND;
    // if we have output redirection:
    if (tokenized[tokenized.size()-2][0] == '>') {
      // open the output fd, redirect the output, and execute the command:
      close(STDOUT_FILENO);
      int fd = open(tokenized.back().c_str(), FFLAGS, RW_PERMS);
      dup2(fd, STDOUT_FILENO); // redirect fd to stdout (STDOUT_FILENO = 1)
      ARGC -= 2; // reduce arg length by 2 (since last 2 args are '>' and the filename)
    }
    // if we have input redirection:
    if (tokenized[tokenized.size() - 2] == "<") {
      // open the output fd, redirect the output, and execute the command:
      close(STDIN_FILENO);
      int fd = open(tokenized.back().c_str(), O_RDONLY); // no need to have write permissions
      dup2(fd, STDIN_FILENO); // redirect fd to stdout (STDOUT_FILENO = 1)
      ARGC -= 2; // reduce arg length by 2 (since last 2 args are '>' and the filename)
    }
  }

  // construct arg list:
  char* args[ARGC];
  args[ARGC-1] = NULL;
  for (int i = 0; i < ARGC-1; i++) {
    args[i] = (char*) tokenized[i].c_str();
  }

  // execute the command:
  execvp(args[0], args);

  // if we're still here, there has been an error in the execution and we need
  // to kill the current process:
  print_error(1);
  exit(-1);
}

std::vector<std::string> splitByPipe(const std::string& input) {
  bool background = false;
  std::vector<std::string> output;
  int start = 0;
  for (int i = 0; i < input.size(); i++) {
    // if process should be a background process:
    if (input[i] == '&') background = true;

    /* neat trick for handling multiple commands:
     * since extra input redirected from last command doesn't matter,
     * we can just pretend the ';' is just another pipe and be lazy */
    if (input[i] == '|' || input[i] == ';') {
      output.push_back(input.substr(start, i-start));
      start = i+1;
      // make sure we don't start on a space character:
      if (start < input.size() && input[start] == ' ') start++;
    }
    // if we see an open quote, ignore input until we find a closing quote:
    else if (input[i] == '\"') {
      while (i < input.size() && input[++i] != '\"');
    }
  }

  // get the rest of the input string, if there is any:
  if (start < input.size()-1) {
    output.push_back(input.substr(start));
  }

  // append "&" to end of output if background process, "-" otherwise:
  output.push_back(background ? "&" : "-");

  return output;
}

std::vector<std::string> tokenize(const std::string& input) {
  // create the string vector
  std::vector<std::string> tokens;
  int start = 0, end = 0;
  for (unsigned int i = 0; i < input.size(); i++) {
    // check (and split) by spaces:
    if (input[i] == ' ') {
      end = i;
      tokens.push_back(input.substr(start, end-start));
      start = i+1;
    }
    // properly tokenize quotation marks:
    else if (input[i] == '\"') {
      start = ++i; // increment i to ignore opening quotation mark
      while (i < input.size() && input[i] != '\"') i++; // find closing quotation mark
      end = i;
      tokens.push_back(input.substr(start, end-start));
      start = ++i;

      // make sure we don't start on a space character:
      if (start < input.size() && input[start] == ' ') start++;
    }
  }
  // get the rest of the string, if there is any:
  if (start < input.size()-1) {
    tokens.push_back(input.substr(start));
  }

  return tokens;
}

void handle_cd(const std::string dir) {
  // no arg provided, cd to home directory (as the unix shell does):
  if (dir == "") {
    chdir(getenv("HOME"));
    return;
  }

  // if arg provided, change the directory appropriately.
  // if no such directory, print an error message:
  if (chdir(dir.c_str()) == -1) {
    print_error(2);
  }
}

void handle_pwd() {
  printf("%s\n", getcwd(NULL, 0));
}

/* ---------- UTILITY FUNCTIONS ---------- */

void handle_color(std::string color) {
  if (COLORS.find(color) == COLORS.end()) {
    printf("[color] error: no such color found.\n");
    return;
  }
  printf("%s", COLORS.at(color).c_str());
}

void clear_screen() {
  #ifdef WINDOWS
  std::system("cls");
  #else
  std::system("clear");
  #endif
}

void exitSignalHandler(int signal) {
  // handle the exit signal by printing a nice message to the user:
  printf("\x1b[0m\n\nexit signal received. quitting...\n");
  printf("--- thank you for using \x1b[0;31mm\x1b[0;32my\x1b[0;33mS\x1b[0;34mh\x1b[0;35me\x1b[0;36ml\x1b[0ml ---\n");
  exit(signal);
}

void print_error(const int err_code) {
  printf("\x1b[0;31mError (%d):\x1b[0m %s", err_code, ERROR_STR[err_code].c_str());
}
