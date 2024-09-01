#include <iostream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

/*
 Muestra el prompt de la shell en la consola.
*/
void showPrompt() {
    std::cout << "mishell:$ ";
}

/*parseCommand() 
  Parsea el comando ingresado por el usuario y lo divide en tokens.
  @input El comando completo ingresado por el usuario.
  @return Un vector de strings que contiene los diferentes tokens del comando.
*/
std::vector<std::string> parseCommand(const std::string& input) {
    std::istringstream iss(input);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

/*isRedirection() 
  Verifica si el comando contiene redirecciones de entrada o salida.
  @args Vector de strings que contiene los tokens del comando.
  @inFile Descriptor de archivo para la redirección de entrada.
  @outFile Descriptor de archivo para la redirección de salida.
  @return true si la redirección es válida, false en caso contrario.
*/
bool isRedirection(const std::vector<std::string>& args, int& inFile, int& outFile) {
    inFile = -1;
    outFile = -1;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "<") { // Redirección de entrada
            if (i + 1 < args.size()) {
                inFile = open(args[i + 1].c_str(), O_RDONLY);
                if (inFile < 0) {
                    std::cerr << "Error: No se pudo abrir el archivo " << args[i + 1] << std::endl;
                    return false;
                }
            }
        } else if (args[i] == ">") { // Redirección de salida
            if (i + 1 < args.size()) {
                outFile = open(args[i + 1].c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (outFile < 0) {
                    std::cerr << "Error: No se pudo abrir el archivo " << args[i + 1] << std::endl;
                    return false;
                }
            }
        }
    }

    return true;
}

/*executeCommand()
  Ejecuta un comando que puede incluir redirección de entrada o salida.
  @args Vector de strings que contiene los tokens del comando.
*/
void executeCommand(const std::vector<std::string>& args) {
    int inFile, outFile;

    if (!isRedirection(args, inFile, outFile)) {
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "Error: Fallo en fork." << std::endl;
    } else if (pid == 0) { // Proceso hijo
        if (inFile != -1) {
            dup2(inFile, STDIN_FILENO);
            close(inFile);
        }

        if (outFile != -1) {
            dup2(outFile, STDOUT_FILENO);
            close(outFile);
        }

        std::vector<char*> argv;
        for (const auto& arg : args) {
            if (arg == "<" || arg == ">") break;
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        if (execvp(argv[0], argv.data()) == -1) {
            std::cerr << "Error: Comando no encontrado." << std::endl;
        }
        exit(EXIT_FAILURE);
    } else { // Proceso padre
        if (inFile != -1) close(inFile);
        if (outFile != -1) close(outFile);
        wait(nullptr);
    }
}

/*executePipedCommands()
  Ejecuta dos comandos conectados por un pipe.
  @args Vector de strings que contiene los tokens de ambos comandos separados por un pipe.
*/
void executePipedCommands(const std::vector<std::string>& args) {
    std::vector<std::string> cmd1, cmd2;
    bool foundPipe = false;

    // Separa los comandos
    for (const auto& arg : args) {
        if (arg == "|") {
            foundPipe = true;
            continue;
        }

        if (foundPipe) {
            cmd2.push_back(arg);
        } else {
            cmd1.push_back(arg);
        }
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        std::cerr << "Error: No se pudo crear la tubería." << std::endl;
        return;
    }

    pid_t pid1 = fork();
    if (pid1 < 0) {
        std::cerr << "Error: Fallo en fork." << std::endl;
        return;
    }

    if (pid1 == 0) { // Proceso hijo para el primer comando
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        std::vector<char*> argv;
        for (const auto& arg : cmd1) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        if (execvp(argv[0], argv.data()) == -1) {
            std::cerr << "Error: Comando no encontrado." << std::endl;
        }
        exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 < 0) {
        std::cerr << "Error: Fallo en fork." << std::endl;
        return;
    }

    if (pid2 == 0) { // Proceso hijo para el segundo comando
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[1]);
        close(pipefd[0]);

        std::vector<char*> argv;
        for (const auto& arg : cmd2) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        if (execvp(argv[0], argv.data()) == -1) {
            std::cerr << "Error: Comando no encontrado." << std::endl;
        }
        exit(EXIT_FAILURE);
    }

    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, nullptr, 0);
    waitpid(pid2, nullptr, 0);
}

/*hasPipe()
  Verifica si un comando contiene un pipe '|'.  
  @args Vector de strings que contiene los tokens del comando.
  @return true si el comando contiene un pipe, false en caso contrario.
*/
bool hasPipe(const std::vector<std::string>& args) {
    for (const auto& arg : args) {
        if (arg == "|") {
            return true;
        }
    }
    return false;
}

void changeDirectory(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "Error: faltan argumentos para el comando cd" << std::endl;
        return;
    }
    
    // Usamos `chdir` para cambiar al directorio especificado
    if (chdir(args[1].c_str()) != 0) {
        std::perror("Error al cambiar de directorio");
    }
}


int main() {
    std::string input;
    while (true) {
        showPrompt();
        std::getline(std::cin, input);

        if (input.empty()) {
            continue;
        }

        std::vector<std::string> args = parseCommand(input);

        if (args[0] == "exit") {
            break;
        }

        if (args[0] == "cd") {
            changeDirectory(args);
        } else if (hasPipe(args)) {
            executePipedCommands(args);
        } else {
            executeCommand(args);
        }
    }

    return 0;
}