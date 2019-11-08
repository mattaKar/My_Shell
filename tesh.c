#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <string.h>
#include <errno.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <dlfcn.h>

#define TESH_MAX_PIPES 20
#define TESH_MAX_HOSTNAME 128
#define TESH_MAX_PROMPT 1024
#define TESH_MAX_DIR 512
#define TESH_MAX_CMD 1024
#define TESH_MAX_ARGS 256
#define TESH_MAX_WORD 512


int tesh1(), exec_pipes(char** pipes[], int cnt), smart_cmd(), analyse_cmd(char *cmd), vider_arg_v();
int exec_cd(), exec_fg();  // Les fonctions internes
int arg_c;        // le nombre d'argument dans arg_v
char * arg_v[TESH_MAX_ARGS];    // Le tableau dans lequel on place la commande que l'on souhaite executer
char * arg_vbis[TESH_MAX_ARGS]; // Le tableau qui permet de recuperer la commande interprété avant un pipe/point virgule/&&/|| ...
char * waiter[TESH_MAX_ARGS];   // Le tableau qui permet de recuperer le reste de la commande qui doit être interprété en fonction du séparateur de commande et du résultat de la commande
char * arg_v_store[TESH_MAX_ARGS]; // Mémoire pour les chaines de arg_v
char * arg_vbis_store[TESH_MAX_ARGS]; // Memoire pour les chaines de arg_v_bis
char * waiter_store[TESH_MAX_ARGS];// Memoire pour les chaines de waiter

int I=0;          // Le code de retour de la commande réalisé est stocké dans I
int errortype;    // Les types d'erreurs sont stockés dans cette variable
int interactive;  // Vaut 1 si le mode est interactif, 0 si on exécute un script ou STDIN n'est pas un terminal
int exitonerror;  // Dans le cas ou l'ont utilise le mode -e
FILE* inputFile;  //Entrée des commandes
int readline_used=0; // Vaut 1 si l'option -r est demandée par l'utilisateur
int lastbackground;

typedef char* (*RL_type)(const char*);
typedef int (*AH_type)(const char*);
RL_type readline_ptr;
AH_type add_history_ptr;

int main(int argc, char *argv[]) {
    interactive=1;
    exitonerror=0;
    inputFile=stdin;
    
    // Alloue la mémoire pour arg_v, arg_vbis et waiter
    for (int i=0;i<TESH_MAX_ARGS;i++){
        arg_v_store[i]=malloc(TESH_MAX_WORD*sizeof(char));
    }
    for (int i=0;i<TESH_MAX_ARGS;i++){
        arg_vbis_store[i]=malloc(TESH_MAX_WORD*sizeof(char));
    }
    for (int i=0;i<TESH_MAX_ARGS;i++){
        waiter_store[i]=malloc(TESH_MAX_WORD*sizeof(char));
    }
    
    //Réglage des différents modes interactif, sortie sur erreur...
    if (isatty(0) == 0) interactive = 0;
    if (argc>1) {
        int i=1;
        while (i!=argc) {
            if (!strcmp(argv[i],"-e")) {
                exitonerror=1;
            } else if (!strcmp(argv[i],"-r")) {
                void *handle;
                if (!(handle=dlopen("libreadline.dylib", RTLD_LAZY)) && // MacOS
                    !(handle=dlopen("libreadline.so", RTLD_LAZY))) { // Linux
                    printf("Cannot load libreadline: %s\n", dlerror());
                } else {
                    readline_ptr = dlsym(handle, "readline");
                    if (!readline_ptr) {
                        printf("Cannot find readline: %s\n", dlerror());
                    } else {
                        add_history_ptr = dlsym(handle, "add_history");
                        if (!add_history_ptr) {
                            printf("Cannot find add_history: %s\n", dlerror());
                        } else {
                            //On a trouvé les fonctions, on peut utiliser readline
                            readline_used=1;
                        }
                    }
                }
            } else {
                // Script
                // Ouvrir le fichier en stdin
                interactive = 0;
                if ((inputFile = freopen(argv[i],"r",stdin)) == NULL) {
                    perror(argv[i]);
                    exit(1);
                }
            }
            i++;
        }
    }
    
    tesh1();
    
    return 0;
}


//fonctions de création et de suppression des différents tableau (allocation par malloc)

int create_arg_v(){
    for (int i=0;i<TESH_MAX_ARGS;i++){
        arg_v[i]=arg_v_store[i];
    }
    return EXIT_SUCCESS;
    
}

int create_arg_vbis(){
    for (int i=0;i<TESH_MAX_ARGS;i++){
        arg_vbis[i]=arg_vbis_store[i];
    }
    return EXIT_SUCCESS;
}

int create_waiter(){
    for (int i=0;i<TESH_MAX_ARGS;i++){
        waiter[i]=waiter_store[i];
    }
    return EXIT_SUCCESS;
}

int delete_arg_v(){
    return EXIT_SUCCESS;
}

int delete_waiter(){
    return EXIT_SUCCESS;
}

int delete_arg_vbis(){
    return EXIT_SUCCESS;
}




int tesh1(){
    
    char hostname[TESH_MAX_HOSTNAME];
    char workingDir[TESH_MAX_DIR];
    char prompt[TESH_MAX_PROMPT];
    char** pipes[TESH_MAX_PIPES];
    
    gethostname(hostname,TESH_MAX_HOSTNAME); //Nom d'hôte pour l'entête des requêtes
    
    while(1){
        char cmd[TESH_MAX_CMD]={}; //Contient la commande que va taper l'utilisateur
        int J;
        sprintf(prompt,"%s@%s:%s$ ",getenv("USER"), hostname, getcwd(workingDir, TESH_MAX_DIR));
        if (interactive==1 && !readline_used) {
            printf("%s", prompt);
        }
        if(readline_used){
            char* line = readline_ptr(prompt);
            strcpy(cmd, line);
            free(line);
            add_history_ptr(cmd);
        }
        else if (fscanf(inputFile, "%[^\n]", cmd) == EOF) {
            if (interactive==1) printf("exit\n");
            exit(0);
        }
        if(!readline_used){
            while(fgetc(inputFile)!='\n');
        }
        
        create_arg_v();
        // Lecture de la commande entrée et séparation de la commande dans le tableau arg_v
        analyse_cmd(cmd);
        J=0;
        
        errortype = 0;
        
        while (arg_c > 0) {
            I=3;
            create_arg_vbis();
            smart_cmd();
            if ((J==0) || (J==1 && errortype != 0) || (J==2 && errortype == 0)) {
                //Builtins
                if(!strcmp(arg_vbis[0],"cd")) {
                    exec_cd();
                } else if(!strcmp(arg_vbis[0],"exit")) {
                    exit(0);
                } else if (!strcmp(arg_vbis[0],"fg")) {
                    exec_fg();
                } else {
                    //Recherche les pipes
                    pipes[0]=arg_vbis;
                    int k=0;
                    int l=0;
                    pipes[l]=&arg_vbis[k];
                    while (arg_vbis[k+1]!=NULL) {
                        if (!strcmp(arg_vbis[k+1],"|")) {
                            if (l == TESH_MAX_PIPES) {
                                break;
                            }
                            l++;
                            pipes[l]=&arg_vbis[k+2];
                            arg_vbis[k+1]=NULL;
                        }
                        k++;
                    }
                    exec_pipes(pipes,l+1);
                }
                delete_arg_vbis();
            }
            J=I;
        }
        
        delete_arg_v();
    }
    return EXIT_SUCCESS;
}

int exec_pipes(char** pipes[], int cnt) {
    int numpipe=0;
    int redir_input=0;
    int redir_output=0;
    int redir_output_append=0;
    char* redir_input_file;
    char* redir_output_file;
    int k;
    int input_des;
    int output_des;
    int id_fils;
    int background;
    int pipe_des[2]; // Descripteurs de pipes
    int pipe_des_prev; // Memorise le pipe 0 du précédent
    
    
    // printf("exec_pipes %d\n", cnt);
    while (numpipe!=cnt) {
        char** args=pipes[numpipe];
        background=0;
        
        // Redirections ?
        k=1;
        while (args[k] != NULL) {
            // Redirection input pour la première uniquement
            // Redirection output pour la dernière uniquement
            if ((!strcmp(args[k],"<")) && (numpipe==0)) {
                redir_input=1;
                redir_input_file=args[k+1];
                args[k] = NULL;
            } else if ((!strcmp(args[k],">")) && (numpipe==cnt-1)) {
                redir_output=1;
                redir_output_file=args[k+1];
                args[k] = NULL;
            } else if ((!strcmp(args[k],">>")) && (numpipe==cnt-1)) {
                redir_output=1;
                redir_output_append=1;
                redir_output_file=args[k+1];
                args[k] = NULL;
            } else {
                k++;
            }
        }
        
        // Background ?
        k=1;
        while (args[k] != NULL) {
            if ((!strcmp(args[k],"&"))) {
                background=1;
                args[k]=NULL;
                break;
            } else {
                k++;
            }
        }
        
        if ((cnt>1) && (numpipe < cnt-1)) {
            // Au moins un pipe. On crée le pipe. Sauf pour le dernier.
            pipe(pipe_des);
        }
        
        if((id_fils=fork())==-1) {
            if ((cnt>1) && (numpipe < cnt-1)) {
                close(pipe_des[1]);
                close(pipe_des[0]);
            }
            perror("");
            return EXIT_FAILURE;
        }
        
        if (id_fils==0) {
            // Fermer pipe inutile
            if ((cnt>1) && (numpipe<cnt-1)) {
                close(pipe_des[0]);
            }
            
            if (numpipe == 0) {
                // Premier
                // Redirection d'input ?
                if (redir_input) {
                    if (redir_input_file==NULL) {
                        printf("file name expected\n");
                        return EXIT_FAILURE;
                    }
                    input_des = open(redir_input_file, O_RDONLY, 0600);
                    if (input_des == -1) {
                        perror(redir_input_file);
                        return EXIT_FAILURE;
                    }
                    dup2(input_des, STDIN_FILENO);
                    close(input_des);
                }
                // Si pipe, affecte STDOUT uniquement (premier)
                if (cnt>1) {
                    dup2(pipe_des[1], STDOUT_FILENO);
                    close(pipe_des[1]);
                }
            }
            
            if (numpipe==cnt-1) {
                // Dernier
                if (redir_output) {
                    if (redir_output_file==NULL) {
                        printf("file name expected\n");
                        return EXIT_FAILURE;
                    }
                    if (redir_output_append) {
                        output_des = open(redir_output_file, O_CREAT | O_APPEND | O_WRONLY, 0600);
                    } else {
                        output_des = open(redir_output_file, O_CREAT | O_TRUNC | O_WRONLY, 0600);
                    }
                    if (output_des == -1) {
                        perror(redir_output_file);
                        return EXIT_FAILURE;
                    }
                    dup2(output_des, STDOUT_FILENO);
                    close(output_des);
                }
                
                // Si pipe, affecte STDIN uniquement, avec le pipe du précédent
                if (cnt>1) {
                    dup2(pipe_des_prev, STDIN_FILENO);
                    close(pipe_des_prev);
                }
            }
            
            if ((cnt>1) && (numpipe!=0) && (numpipe<(cnt-1)))  {
                // Ni premier ni dernier
                // Si pipe, affecte STDIN et STDOUT
                dup2(pipe_des_prev, STDIN_FILENO);
                close(pipe_des_prev);
                dup2(pipe_des[1], STDOUT_FILENO);
                close(pipe_des[1]);
            }
            
            errortype = execvp(pipes[numpipe][0], (char **) pipes[numpipe]);
            if (errortype==-1) {
                printf("%s: command not found\n", pipes[numpipe][0]);
            }
        } else {
            // Père
            if (cnt>1) {
                // Fermer les pipes
                if (numpipe != 0) close(pipe_des_prev);
                if (numpipe < cnt-1) close(pipe_des[1]);
                // On mémorise le pipe 0
                pipe_des_prev=pipe_des[0];
            }
            if (background) {
                printf("[%d]\n", id_fils);
                fflush(stdout);
                lastbackground=id_fils;
            } else {
                int status;
                if (waitpid(id_fils, &status, 0) == -1){
                    perror("");
                    return EXIT_FAILURE;
                }
                if (WIFEXITED(status)) {
                    errortype=WEXITSTATUS(status);
                    if (errortype!=0 && exitonerror) {
                        exit(1);
                    }
                }
            }
        }
        numpipe++;
    }
    return EXIT_SUCCESS;
}

int analyse_cmd(char *cmd){
    // On sépare la commande entrée en dans le tableau arg_v, chaque case correspond
    // à un "mot", on cherche les token de fin de mot \0 pour effectuer la séparation
    
    char *currentWord=arg_v[0];
    char *currentCharInW=currentWord;
    char *currentChar=cmd;
    
    arg_c=0;
    
    while(*currentChar!='\0'){
        while(*currentChar==' ') {
            currentChar++;
        }
        while(*currentChar!=' ' && *currentChar!='\0'){
            *currentCharInW++ = *currentChar++;
        }
        currentChar++;
        *currentCharInW='\0';
        //printf("%s\n",arg_v[arg_c]);
        arg_c++;
        currentWord=arg_v[arg_c];
        currentCharInW=currentWord;
    }
    
    arg_v[arg_c]=NULL;
    return EXIT_SUCCESS;
}

int smart_cmd(){
    
    // Tant que toute la commande n'a pas été traité, on continue le traitement,
    // si on rencontre un mot spécial comme ;, || ou && on met fin a la boucle
    // et on sépare arg_v en 2 tableaux arg_vbis et waiter
    int i=0;
    int a;
    int bouclestop=0;
    while( i<arg_c && bouclestop==0){
        if (strcmp(arg_v[i],";")==0 || strcmp(arg_v[i],"||")==0 || strcmp(arg_v[i],"&&")==0){
            if (strcmp(arg_v[i],";")==0){
                I=0;
            } else if (strcmp(arg_v[i],"||")==0){
                I=1;
            } else {
                I=2;
            }
            bouclestop=1;
            arg_vbis[i]=NULL;
            i++;
        } else {
            strcpy(arg_vbis[i],arg_v[i]);
            i++;
        }
    }
    
    if(i==arg_c){
        arg_vbis[i]=NULL;
    }
    a=i;
    
    
    create_waiter();
    
    int c=0;
    while(i<arg_c){
        strcpy(waiter[c],arg_v[i]);
        i++;
        c++;
    }
    arg_c=arg_c-a;
    
    
    delete_arg_v();
    create_arg_v();
    
    for (int i=0;i<c;i++){
        strcpy(arg_v[i], waiter[i]);
    }
    
    delete_waiter();
    return EXIT_SUCCESS;
}

int exec_cd(){
    // Commande interne cd, dans ce cas on change de repertoire en fonction du chemin renseigné
    if (arg_vbis[1]==NULL || strcmp(arg_vbis[1],"~")==0){
        chdir(getenv("HOME"));
    } else {
        if (chdir(arg_vbis[1]) == -1) {
            perror("");
            errortype=-1;
        }
    }
    return EXIT_SUCCESS;
}


int exec_fg() {
    // Commande interne consistant à replacer un processus en premier plan
    int status;
    int child;
    int id_fils=lastbackground;
    if (arg_vbis[1] != NULL) {
        id_fils=atoi(arg_vbis[1]);
    }
    child=waitpid(id_fils, &status, 0);
    if (child!=id_fils) {
        printf("fg: no such job\n");
    } else {
        printf("[%d->%d]\n",id_fils,WEXITSTATUS(status));
    }
    return EXIT_SUCCESS;
}

