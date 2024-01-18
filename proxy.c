#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "./simpleSocketAPI.h"

#define MY_ADDR "127.0.0.1"
#define MY_PORT "0"
#define BACKLOG_LEN 1
#define MAX_BUFFER_LEN 1024
#define MAX_HOST_LEN 64
#define MAX_PORT_LEN 64
#define FTP_PORT "21"

void printMemory(const void *ptr, size_t size);
void readClient(int *status, int socketDesc, char *buffer);
void writeClient(int socketDesc, char *buffer);
void readServer(int *status, int socketDesc, char *buffer);
void writeServer(int socketDesc, char *buffer);

int main() {
    int hostPart1, hostPart2, hostPart3, hostPart4, portDigit1, portDigit2, port;
    // Les parties de l'adresse IP et le numéro de port pour une connexion FTP

    char clientCommAddr[MAX_HOST_LEN];
    // Adresse client pour la communication

    char clientCommPort[MAX_PORT_LEN];
    // Numéro de port client pour la communication

    int status;
    // Variable pour stocker le statut des opérations (retours de fonction, etc.)

    char serverAddr[MAX_HOST_LEN];
    // Adresse IP du serveur

    char serverPort[MAX_PORT_LEN];
    // Numéro de port du serveur

    int rendezvousSocketDesc;
    // Descripteur de socket pour la socket de rendez-vous

    int communicationSocketDesc;
    // Descripteur de socket pour la communication avec le client

    int ftpCtrlSocketDesc;
    // Descripteur de socket pour le contrôle FTP

    int ftpDataSocketDesc;
    // Descripteur de socket pour les données FTP

    int clientDataSocketDesc;
    // Descripteur de socket pour les données du client

    struct addrinfo hints;
    // Structure pour spécifier des critères lors de la recherche d'informations sur l'adresse

    struct addrinfo *result;
    // Pointeur vers la liste des résultats obtenus après la recherche d'informations sur l'adresse

    struct sockaddr_storage myinfo;
    // Structure pour stocker des informations sur la socket locale

    struct sockaddr_storage clientAddr;
    // Structure pour stocker des informations sur la socket du client

    socklen_t addrLen;
    // Type pour stocker la longueur des adresses utilisées dans les appels système de socket

    char buffer[MAX_BUFFER_LEN];
    // Tampon pour stocker les données à lire ou à écrire


    // Initialisation de la socket de RDV IPv4/TCP
    rendezvousSocketDesc = socket(AF_INET, SOCK_STREAM, 0);
    if (rendezvousSocketDesc == -1) {
        perror("Erreur création socket RDV\n");
        exit(2);
    }

    // Publication de la socket au niveau du système
    // Assignation d'une adresse IP et d'un numéro de port
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
    status = bind(rendezvousSocketDesc, result->ai_addr, result->ai_addrlen);
    if (status == -1) {
        perror("Erreur liaison de la socket de RDV");
        exit(3);
    }
    freeaddrinfo(result);

    // Récupération du nom de la machine et du numéro de port pour affichage à l'écran
    addrLen = sizeof(struct sockaddr_storage);
    status = getsockname(rendezvousSocketDesc, (struct sockaddr *)&myinfo, &addrLen);
    if (status == -1) {
        perror("SERVEUR: getsockname");
        exit(4);
    }
    status = getnameinfo((struct sockaddr *)&myinfo, sizeof(myinfo), serverAddr, MAX_HOST_LEN,
                         serverPort, MAX_PORT_LEN, NI_NUMERICHOST | NI_NUMERICSERV);
    if (status != 0) {
        fprintf(stderr, "error in getnameinfo: %s\n", gai_strerror(status));
        exit(4);
    }

    printf("L'adresse d'écoute est: %s\n", serverAddr);
    printf("Le port d'écoute est: %s\n", serverPort);

    // Définition de la taille du tampon contenant les demandes de connexion
    status = listen(rendezvousSocketDesc, BACKLOG_LEN);
    if (status == -1) {
        perror("Erreur initialisation buffer d'écoute");
        exit(5);
    }

    addrLen = sizeof(struct sockaddr_storage);
    while (true) {
        // Attente de la connexion du client
        communicationSocketDesc = accept(rendezvousSocketDesc, (struct sockaddr *)&clientAddr, &addrLen);
        if (communicationSocketDesc == -1) {
            perror("Erreur lors de l'acceptation de la connexion du client");
            exit(6);
        }

        // Envoi du message d'accueil au client
        if (writeClient(communicationSocketDesc, "220 Connecté au proxy, entrez votre username@server\n") == -1) {
            perror("Erreur lors de l'envoi du message au client");
            exit(6);
        }

        // Lecture de l'identifiant et du serveur
        readClient(&status, communicationSocketDesc, buffer);

        // Décomposition de l'identifiant et du serveur
        char login[50], ftpServerName[50];
        memset(login, '\0', sizeof(login));
        memset(ftpServerName, '\0', sizeof(ftpServerName));
        sscanf(buffer, "%48[^@]@%48s", login, ftpServerName);
        sprintf(login, "%s\n", login);

        // Connexion au serveur ftp
        if (connect2Server(ftpServerName, FTP_PORT, &ftpCtrlSocketDesc) == -1) {
            perror("Erreur lors de la connexion au serveur FTP");
            exit(6);
        }

        // Lecture de la réponse du serveur ftp
        readServer(&status, ftpCtrlSocketDesc, buffer);

        // Envoi de l'identifiant au serveur ftp
        writeServer(ftpCtrlSocketDesc, login);

        // Lecture de la réponse du serveur ftp
        readServer(&status, ftpCtrlSocketDesc, buffer);

        // Envoi de la réponse du serveur ftp au client
        if (writeClient(communicationSocketDesc, buffer) == -1) {
            perror("Erreur lors de l'envoi de la réponse au client");
            exit(6);
        }

        // Lecture du mot de passe du client
        readClient(&status, communicationSocketDesc, buffer);

        // Envoi du mot de passe au serveur ftp
        writeServer(ftpCtrlSocketDesc, buffer);

        // Lecture de la réponse du serveur ftp
        readServer(&status, ftpCtrlSocketDesc, buffer);

        // Envoi de la réponse du serveur ftp au client
        if (writeClient(communicationSocketDesc, buffer) == -1) {
            perror("Erreur lors de l'envoi de la réponse au client");
            exit(6);
        }

        // Lecture de la demande SYST du client
        readClient(&status, communicationSocketDesc, buffer);

        // Envoi de la demande SYST au serveur ftp
        writeServer(ftpCtrlSocketDesc, buffer);

        // Lecture de la réponse du serveur ftp
        readServer(&status, ftpCtrlSocketDesc, buffer);

        // Envoi de la réponse du serveur ftp au client
        if (writeClient(communicationSocketDesc, buffer) == -1) {
            perror("Erreur lors de l'envoi de la réponse au client");
            exit(6);
        }

        // Lecture des demandes du client
        for (readClient(&status, communicationSocketDesc, buffer);; readClient(&status, communicationSocketDesc, buffer)) {
            // Si la demande est QUIT, sortir de la boucle
            if (strncmp(buffer, "QUIT", 4) == 0) {
                break;
            }

            // Envoyer la demande du client au serveur ftp
            writeServer(ftpCtrlSocketDesc, buffer);

            // Lire la réponse du serveur ftp
            readServer(&status, ftpCtrlSocketDesc, buffer);
            
            // Envoyer la réponse du serveur ftp au client
            if (writeClient(communicationSocketDesc, buffer) == -1) {
                perror("Erreur lors de l'envoi de la réponse au client");
                exit(6);
            }
        }

        // Fermeture de la connexion avec le serveur ftp et le client
        close(ftpCtrlSocketDesc);
        close(communicationSocketDesc);
    }

    // Fermeture de la socket de rendez-vous
    close(rendezvousSocketDesc);
    return 0;
}

void readClient(int *status, int socketDesc, char *buffer) {
    *status = read(socketDesc, buffer, MAX_BUFFER_LEN - 1);
    if (*status == -1) {
        perror("Problème de lecture\n");
        exit(3);
    }
    buffer[*status] = '\0';
    printf("<--C %s", buffer);
}

void readServer(int *status, int socketDesc, char *buffer) {
    *status = read(socketDesc, buffer, MAX_BUFFER_LEN - 1);
    if (*status == -1) {
        perror("Problème de lecture\n");
        exit(3);
    }
    buffer[*status] = '\0';
    printf("P<--S %s", buffer);
}

void writeClient(int socketDesc, char *buffer) {
    write(socketDesc, buffer, strlen(buffer));
    printf("P-->C %s", buffer);
}

void writeServer(int socketDesc, char *buffer) {
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
