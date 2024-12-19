#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
const char *sysname = "dash";


enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n",
		   command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");

	for (i = 0; i < 3; i++) {
		printf("\t\t%d: %s\n", i,
			   command->redirects[i] ? command->redirects[i] : "N/A");
	}

	printf("\tArguments (%d):\n", command->arg_count);

	for (i = 0; i < command->arg_count; ++i) {
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	}

	if (command->next) {
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
	if (command->arg_count) {
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}

	for (int i = 0; i < 3; ++i) {
		if (command->redirects[i])
			free(command->redirects[i]);
	}

	if (command->next) {
		free_command(command->next);
		command->next = NULL;
	}

	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt(void) {
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);

	// trim left whitespace
	while (len > 0 && strchr(splitters, buf[0]) != NULL) {
		buf++;
		len--;
	}

	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL) {
		// trim right whitespace
		buf[--len] = 0;
	}

	// auto-complete
	if (len > 0 && buf[len - 1] == '?') {
		command->auto_complete = true;
	}

	// background
	if (len > 0 && buf[len - 1] == '&') {
		command->background = true;
	}

	char *pch = strtok(buf, splitters);
	if (pch == NULL) {
		command->name = (char *)malloc(1);
		command->name[0] = 0;
	} else {
		command->name = (char *)malloc(strlen(pch) + 1);
		strcpy(command->name, pch);
	}

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1) {
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// trim left whitespace
		while (len > 0 && strchr(splitters, arg[0]) != NULL) {
			arg++;
			len--;
		}

		// trim right whitespace
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL) {
			arg[--len] = 0;
		}

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// piping to another command
		if (strcmp(arg, "|") == 0) {
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0) {
			// handled before
			continue;
		}

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<') {
			redirect_index = 0;
		}

		if (arg[0] == '>') {
			if (len > 1 && arg[1] == '>') {
				redirect_index = 2;
				arg++;
				len--;
			} else {
				redirect_index = 1;
			}
		}

		if (redirect_index != -1) {
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 &&
			((arg[0] == '"' && arg[len - 1] == '"') ||
			 (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}

		command->args =
			(char **)realloc(command->args, sizeof(char *) * (arg_index + 1));

		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;

	// increase args size by 2
	command->args = (char **)realloc(
		command->args, sizeof(char *) * (command->arg_count += 2));

	// shift everything forward by 1
	for (int i = command->arg_count - 2; i > 0; --i) {
		command->args[i] = command->args[i - 1];
	}

	// set args[0] as a copy of name
	command->args[0] = strdup(command->name);

	// set args[arg_count-1] (last) to NULL
	command->args[command->arg_count - 1] = NULL;

	return 0;
}

void prompt_backspace(void) {
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}

void autocomplete(char *buf, size_t *index);
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
	size_t index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &=
		~(ICANON |
		  ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	show_prompt();
	buf[0] = 0;

	while (1) {
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		// handle tab
		if (c == 9) {
			buf[index++] = '?'; // autocomplete
			autocomplete(buf, &index);
			continue;
		}

		// handle backspace
		if (c == 127) {
			if (index > 0) {
				prompt_backspace();
				index--;
			}
			continue;
		}

		if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
			continue;
		}

		// up arrow
		if (c == 65) {
			while (index > 0) {
				prompt_backspace();
				index--;
			}

			char tmpbuf[4096];
			printf("%s", oldbuf);
			strcpy(tmpbuf, buf);
			strcpy(buf, oldbuf);
			strcpy(oldbuf, tmpbuf);
			index += strlen(buf);
			continue;
		}

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}

	// trim newline from the end
	if (index > 0 && buf[index - 1] == '\n') {
		index--;
	}

	// null terminate string
	buf[index++] = '\0';

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

int main(void) {
	while (1) {
		struct command_t *command = malloc(sizeof(struct command_t));

		// set all bytes to 0
		memset(command, 0, sizeof(struct command_t));

		int code;
		code = prompt(command);
		if (code == EXIT) {
			break;
		}

		code = process_command(command);
		if (code == EXIT) {
			break;
		}

		free_command(command);
	}

	printf("\n");
	return 0;
}

int process_command(struct command_t *command) {
	int r;

	if (strcmp(command->name, "") == 0) {
		return SUCCESS;
	}

	if (strcmp(command->name, "exit") == 0) {
		return EXIT;
	}

	if (strcmp(command->name, "cd") == 0) {
		if (command->arg_count > 0) {
			r = chdir(command->args[0]);
			if (r == -1) {
				printf("-%s: %s: %s\n", sysname, command->name,
					   strerror(errno));
			}

			return SUCCESS;
		}
	}

	pid_t pid = fork();

	// child
	if (pid == 0) {
		/// This shows how to do exec with environ (but is not available on MacOs)
		// extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// TODO: do your own exec with path resolving using execv()
		// do so by replacing the execvp call below

		if (command->redirects[0]) { 
			// Input redirection: <
			char *input_file = command->redirects[0];

			// If there is space, the file is included in command argumetns
			if (!input_file || *input_file == '\0') {
		        input_file = command->args[command->arg_count - 2];
			}

			FILE *input = fopen(input_file, "r");
			if (!input) {
				perror("Error: Input file does not exist");
				exit(EXIT_FAILURE);
			}
			dup2(fileno(input), STDIN_FILENO);
			fclose(input);
		}

		if (command->redirects[1]) { 
			// Output redirection: >
			char *output_file = command->redirects[1];

			FILE *output = fopen(output_file, "w");
			if (!output) {
				perror("Error: Output file could not be opened");
				exit(EXIT_FAILURE);
			}
			dup2(fileno(output), STDOUT_FILENO);
			fclose(output);
		}

		if (command->redirects[2]) { 
			// Append redirection: >>
			char *append_file = command->redirects[2];

			FILE *append = fopen(append_file, "a");
			if (!append) {
				perror("Error: Append file could not be opened");
				exit(EXIT_FAILURE);
			}
			dup2(fileno(append), STDOUT_FILENO);
			fclose(append);
		}

		int path_length = 1024;
		char *path = getenv("PATH");
		char *path_copy = strdup(path);
		char *token = strtok(path_copy, ":");
		char command_path[path_length];

		while (token != NULL) {
			strcpy(command_path, token);
			char *command_name = command->name;
			snprintf(command_path, sizeof(command_path), "%s/%s", token, command_name);
            execv(command_path, command->args);
            token = strtok(NULL, ":");
		}

		printf("%s not found", command -> name);
		free(path_copy);		
		exit(0);
	} 
	else {
		// TODO: implement background processes here
		// Parent process
		if (command -> background == false) {
			wait(NULL);
		}
		else {}

		}

	// TODO: your implementation here
	//implemented piping here. 
	if (command->next) {
			int pipefd[2];
			if (pipe(pipefd) == -1) {
				perror("pipe");
				exit(EXIT_FAILURE);
			}

			pid_t pid1 = fork();
			if (pid1 == 0) {
				close(pipefd[0]); 
				dup2(pipefd[1], STDOUT_FILENO); 
				close(pipefd[1]); 
				execvp(command->name, command->args);
				perror("execvp"); 
				exit(EXIT_FAILURE);
			}

			pid_t pid2 = fork();
			if (pid2 == 0) {
				close(pipefd[1]); 
				dup2(pipefd[0], STDIN_FILENO); 
				close(pipefd[0]); 
				execvp(command->next->name, command->next->args);
				perror("execvp"); 
				exit(EXIT_FAILURE);
			}

			// Parent process
			close(pipefd[0]); 
			close(pipefd[1]);
			waitpid(pid1, NULL, 0); 
			waitpid(pid2, NULL, 0); 
			return SUCCESS;
		}

		wait(0); // wait for child process to finish
		return SUCCESS;
	}




	}

	// TODO: your implementation here

	perror("execvp");
	exit(EXIT_FAILURE);


	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}

void autocomplete(char *buf, size_t *index) { 
    char *path = getenv("PATH");
    if (!path) return;    
	char *path_copy = strdup(path);
    char *token = strtok(path_copy, ":");

    char uncompleted_command[512];
    char *question_mark = strchr(buf, '?');
  
    char *matches[512];
    int count = 0;

    if (question_mark) {
        size_t len = question_mark - buf; 
        strncpy(uncompleted_command, buf, len);
        uncompleted_command[len] = '\0';

        while (token != NULL) {
            DIR *dir = opendir(token);
            if (dir) {
                struct dirent *directory;
                while ((directory = readdir(dir)) != NULL) {
                    if (strcmp(directory->d_name, ".") == 0 || strcmp(directory->d_name, "..") == 0) {
                        continue;
                    }
                    if (strncmp(directory->d_name, uncompleted_command, strlen(uncompleted_command)) == 0) {
						
                        matches[count] = strdup(directory->d_name);
                        count++;
                    }
                }
                closedir(dir);
            }
            token = strtok(NULL, ":");
        }

        if (count == 1) {
			buf[len] = '\0';
            size_t u_len = strlen(uncompleted_command);
			strcat(buf, matches[0] + u_len);
     	    *index = strlen(buf);

            printf("%s", matches[0] + u_len);
            free(matches[0]);
        } 
		
		else if (count > 1) {
			
			printf("\n");
			buf[0] = '\0'; 
            *index = 0;
			
            for (int i = 0; i < count; i++) {
                printf("%s\n", matches[i]);
                free(matches[i]);
			}	
		} 
		
		else {
            printf("\nNo matches found\n");

			buf[0] = '\0';   
    		*index = 0;

			printf("%s@%s:%s %s$ %s", getenv("USER"), "hostname", getcwd(NULL, 0), sysname, buf);
		}
    } 
	else {
        DIR *dir = opendir(".");
        
		if (dir) {
            struct dirent *directory;
            while ((directory = readdir(dir)) != NULL) {
                if (strcmp(directory->d_name, ".") != 0 && strcmp(directory->d_name, "..") != 0) {
                    printf(" %s\n", directory->d_name);
                }
            }
            closedir(dir);
    	}
        
        buf[0] = '\0';
        *index = 0;
    }

    free(path_copy);
}
