#include  <stdio.h>
#include  <stdlib.h>
#include  <sys/socket.h>
#include  <netdb.h>
#include  <string.h>
#include  <unistd.h>
#include  <stdbool.h>
#include "./simpleSocketAPI.h"

#define MY_ADDR "127.0.0.1"
#define MY_PORT "0"
#define BACKLOG_LEN 1
#define MAX_BUFFER_LEN 1024
#define MAX_HOST_LEN 64
#define MAX_PORT_LEN 64
#define FTP_PORT "21"

void printMemory(const void *ptr, size_t size);
void readClient(int* status, int socketDesc, char* buffer);
void writeClient(int socketDesc, char* buffer);
void readServer(int* status, int socketDesc, char* buffer);
void writeServer(int socketDesc, char* buffer);

int main() {
    int h1, h2, h3, h4, p1, p2, port;
    char serverCommAddr[MAX_HOST_LEN];
    char serverCommPort[MAX_PORT_LEN];
    int status;
    char serverAddr[MAX_HOST_LEN];
    char serverPort[MAX_PORT_LEN];
    int rdvSocketDesc;
    int comSocketDesc;
    int ftpCtrlSocketDesc;
    int ftpDataSocketDesc;
    int clientDataSocketDesc;
    struct addrinfo hints;
    struct addrinfo *result;
    struct sockaddr_storage myinfo;
    struct sockaddr_storage clientAddr;
    socklen_t addrLen;
    char buffer[MAX_BUFFER_LEN];

    // Initialisation de la socket de RDV IPv4/TCP
    rdvSocketDesc = socket(AF_INET, SOCK_STREAM, 0);
    if (rdvSocketDesc == -1) {
        perror("Erreur création socket RDV\n");
        exit(2);
    }

    // Publication de la socket au niveau du système
    // Assignation d'une adresse IP et un numéro de port
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;

    // Récupération des informations du serveur
    status = getaddrinfo(MY_ADDR, MY_PORT, &hints, &result);
    if (status) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(1);
    }

    // Publication de la socket
    status = bind(rdvSocketDesc, result->ai_addr, result->ai_addrlen);
    if (status == -1) {
        perror("Erreur liaison de la socket de RDV");
        exit(3);
    }
    freeaddrinfo(result);

    // Récupération du nom de la machine et du numéro de port pour affichage à l'écran
    addrLen = sizeof(struct sockaddr_storage);
    status = getsockname(rdvSocketDesc, (struct sockaddr *) &myinfo, &addrLen);
    if (status == -1) {
        perror("SERVEUR: getsockname");
        exit(4);
    }
    status = getnameinfo((struct sockaddr*)&myinfo, sizeof(myinfo), serverAddr, MAX_HOST_LEN,
                         serverPort, MAX_PORT_LEN, NI_NUMERICHOST | NI_NUMERICSERV);
    if (status != 0) {
        fprintf(stderr, "error in getnameinfo: %s\n", gai_strerror(status));
        exit(4);
    }

    printf("L'adresse d'écoute est: %s\n", serverAddr);
    printf("Le port d'écoute est: %s\n", serverPort);

    // Definition de la taille du tampon contenant les demandes de connexion
    status = listen(rdvSocketDesc, BACKLOG_LEN);
    if (status == -1) {
        perror("Erreur initialisation buffer d'écoute");
        exit(5);
    }

    addrLen = sizeof(struct sockaddr_storage);
   while (true) {
        // Attente connexion du client
        // Lorsque demande de connexion, creation d'une socket de communication avec le client
        comSocketDesc = accept(rdvSocketDesc, (struct sockaddr *) &clientAddr, &addrLen);
        if (comSocketDesc == -1) {
            perror("Erreur lors de l'acceptation de la connexion du client");
            exit(6);
        }

        // Echange de données avec le client connecté
        if (writeClient(comSocketDesc, "220 Connecté au proxy, entrez votre username@server\n") == -1) {
            perror("Erreur lors de l'envoi du message au client");
            exit(6);
        }

        //Lecture de l'identifiant et du serveur
        readClient(&status, comSocketDesc, buffer);

        //Décomposition
        char login[50], ftpServerName[50];
        memset(login, '\0', sizeof(login));
        memset(ftpServerName, '\0', sizeof(ftpServerName));

        sscanf(buffer, "%48[^@]@%48s", login, ftpServerName);
        sprintf(login, "%s\n", login);

        //Connexion au serveur ftp
        if (connect2Server(ftpServerName, FTP_PORT, &ftpCtrlSocketDesc) == -1) {
            perror("Erreur lors de la connexion au serveur FTP");
            exit(6);
        }

        //Lecture de la réponse
        readServer(&status, ftpCtrlSocketDesc, buffer);

        //Envoi de l'indentifiant
        writeServer(ftpCtrlSocketDesc, login);

        //Lecture de la réponse
        readServer(&status, ftpCtrlSocketDesc, buffer);

        if (writeClient(comSocketDesc, buffer) == -1) {
            perror("Erreur lors de l'envoi de la réponse au client");
            exit(6);
        }

        //Lecture du mdp
        readClient(&status, comSocketDesc, buffer);

        writeServer(ftpCtrlSocketDesc, buffer);

        //Lecture de la réponse
        readServer(&status, ftpCtrlSocketDesc, buffer);

        if (writeClient(comSocketDesc, buffer) == -1) {
            perror("Erreur lors de l'envoi de la réponse au client");
            exit(6);
        }

        //Lecture de la demande SYST
        readClient(&status, comSocketDesc, buffer);

        writeServer(ftpCtrlSocketDesc, buffer);

        //Lecture de la réponse
        readServer(&status, ftpCtrlSocketDesc, buffer);
        
        if (writeClient(comSocketDesc, buffer) == -1) {
            perror("Erreur lors de l'envoi de la réponse au client");
            exit(6);
        }

        //Lecture des demandes
        for (readClient(&status, comSocketDesc, buffer);; readClient(&status, comSocketDesc, buffer)) {
            if (strncmp(buffer, "QUIT", 4) == 0) {
                break;
            }

            writeServer(ftpCtrlSocketDesc, buffer);
            readServer(&status, ftpCtrlSocketDesc, buffer);
            
            if (writeClient(comSocketDesc, buffer) == -1) {
                perror("Erreur lors de l'envoi de la réponse au client");
                exit(6);
            }
        }

        //Fermeture de la connexion
        close(ftpCtrlSocketDesc);
        close(comSocketDesc);
    }

    close(rdvSocketDesc);
    return 0;
}

void readClient(int* status, int socketDesc, char* buffer) {
    *status = read(socketDesc, buffer, MAX_BUFFER_LEN - 1);
    if (*status == -1) {
        perror("Problème de lecture\n");
        exit(3);
    }
    buffer[*status] = '\0';
    printf("<--C %s", buffer);
}

void readServer(int* status, int socketDesc, char* buffer) {
    *status = read(socketDesc, buffer, MAX_BUFFER_LEN - 1);
    if (*status == -1) {
        perror("Problème de lecture\n");
        exit(3);
    }
    buffer[*status] = '\0';
    printf("P<--S %s", buffer);
}

void writeClient(int socketDesc, char* buffer) {
    write(socketDesc, buffer, strlen(buffer));
    printf("P-->C %s", buffer);
}

void writeServer(int socketDesc, char* buffer) {
    write(socketDesc, buffer, strlen(buffer));
    printf("P-->S %s", buffer);
}

void printMemory(const void *ptr, size_t size) {
    const unsigned char *byte = ptr;

    for (size_t i = 0; i < size; i++) {
        printf("%02X ", byte[i]);
    }

    printf("\n");
}
