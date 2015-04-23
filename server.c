#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>

#include "lib.h"

#define HOST "127.0.0.1"
#define PORT 10001

/* Possible running modes */
#define RUN_PARITY_MODE "parity"
#define RUN_HAMMING_MODE "hamming"
#define NORMAL  1
#define PARITY  2
#define HAMMING 3

/* Possible commands */
#define LS "ls\0"
#define CD "cd\0"
#define CP "cp\0"
#define SN "sn\0"
#define EXIT "exit\0"

/* Confirmations */
#define ACK "ACK"
#define NACK "NACK"

/* Other flags */
#define END_TRANSMISSION -1
#define EXITED_NORMALLY 1

/* Bit manipulation */
#define get_bit(x, pos) (((x >> pos) & 1) == 1 ? 1 : 0)

/* Tricks to help at hamming mode */
/* Indexes for data bits */
const int data_bits[8] = {1, 7, 6, 5, 3, 2, 1, 0};
const int first_data_in_byte_2 = 1;
/* Indexes of all control bits */
const int control_bits[4] = {3, 2, 0, 4};
/* Indexes needed to calculate the 0th control bit */
const int c0_bit[6] = {3, 1, 7, 5, 3, 1};
const int first_c0_in_byte_2 = 2;
/* Indexes needed to calculate the 1st control bit */
const int c1_bit[6] = {2, 1, 6, 5, 2, 1};
const int first_c1_in_byte_2 = 2;
/* Indexes needed to calculate the 2nd control bit */
const int c2_bit[5] = {0, 7, 6, 5, 0};
const int first_c2_in_byte_2 = 1;
/* Indexes needed to calculate the 3rd control bit */
const int c3_bit[5] = {4, 3, 2, 1, 0};
const int first_c3_in_byte_2 = 0;

inline int get_ones(char x) 
{
    int no_of_ones = 0;
    while (x) {
        x = x & (x - 1);
        no_of_ones++;
    }

    return (no_of_ones & 1);
}

inline void set_parity(char* seq, int index, int parity)
{
    int i;
    for (i = 0; i < 8; i++) seq[index] &= ~(1 << i);
    if (parity) seq[index] |= 1;

}

inline int get_parity(char *seq, int starting_from, int seq_len)
{   
    int i;
    int nb_ones = 0;
    for (i = starting_from; i < seq_len; i++) {
        nb_ones += get_ones(seq[i]);
    }

    /* Check the parity */
    return (nb_ones & 1);
}

inline int is_parity_correct(msg r)
{
    /* First check the parity of bytes starting at pos 1 in the char seq */
    int calculated_parity = get_parity(r.payload, 1, r.len);
    int received_parity = get_bit(r.payload[0], 0);

    /* Check the results */
    if (calculated_parity == received_parity) return 1;
    return 0;
}

int wait_until_ack(msg* r, msg* t)
{
    int res;

    if (r->len == 5) {
        do {
            /* Send again the package */
            res = send_message(t);
            if (res < 0) {
                perror("[SERVER] Error while sending again\n");
                return -1;
            } else {
            }

            /* Wait for the confirmation */
            res = recv_message(r);
            if (res < 0) {
                perror("[SERVER] Error, confirmation not received\n");
                return -1;
            }

        } while (r->len == 5);
    }

    return 1;
}

int wait_until_correct_parity(msg* r, msg* t)
{
    int res;

    do {
        /* Send NACK */
        sprintf(t->payload, NACK);
        t->len = strlen(t->payload) + 1;
        res = send_message(t);
        if (res < 0) {
            perror("[SERVER] Send NACK error. Exiting.\n");
            return -1;
        } else {
        }

        /* Receive again the package */
        res = recv_message(r);
        if (res < 0) {
            perror("[SERVER] Receive error. Exiting\n");
            return -1;
        }

    } while (!is_parity_correct(*r));

    return 1;
}

int detect_correct_errors_and_decode(msg* r)
{
    int i, error;
    char decoded_message[r->len / 2];

    /* Detect error in case we have one */
    /* Take each and every chunk of 2 bytes and apply hamming algorithm on it */
    int chunk;
    int control_bit0, control_bit1, control_bit2, control_bit3;
    for (chunk = 0; chunk < r->len; chunk += 2) {

        /* Detect error on the current chunk of 2 bytes -> r->payload[chunk] and r->payload[chunk + 1] */
        control_bit0 = control_bit1 = control_bit2 = control_bit3 = 0;

        /* Compute control_bit0 */ 
        for (i = 0; i < first_c0_in_byte_2; i++) {
            control_bit0 += get_bit(r->payload[chunk], c0_bit[i]);
        }
        for (i = first_c0_in_byte_2; i < 6; i++) {
            control_bit0 += get_bit(r->payload[chunk + 1], c0_bit[i]);
        }

        /* Compute control_bit1 */
        for (i = 0; i < first_c1_in_byte_2; i++) {
            control_bit1 += get_bit(r->payload[chunk], c1_bit[i]);
        }
        for (i = first_c1_in_byte_2; i < 6; i++) {
            control_bit1 += get_bit(r->payload[chunk + 1], c1_bit[i]);
        }

        /* Compute control_bit2 */
        for (i = 0; i < first_c2_in_byte_2; i++) {
            control_bit2 += get_bit(r->payload[chunk], c2_bit[i]);
        }
        for (i = first_c2_in_byte_2; i < 5; i++) {
            control_bit2 += get_bit(r->payload[chunk + 1], c2_bit[i]);
        }

        /* Compute control_bit3 */
        for (i = first_c3_in_byte_2; i < 5; i++) {
            control_bit3 += get_bit(r->payload[chunk + 1], c3_bit[i]);
        }

        /* Check if we actually have an error or not */
        control_bit0 %= 2; control_bit1 %= 2;
        control_bit2 %= 2; control_bit3 %= 2;

        error = control_bit0; 
        if (control_bit1) error += control_bit1 << 1;
        if (control_bit2) error += control_bit2 << 2;
        if (control_bit3) error += control_bit3 << 3;
        /* If needed, correct the error */
        if (error) {
            /* Correct the first byte */
            if (error <= 4) { r->payload[chunk] ^= (1 << (4 - error)); }
            /* Correct the second byte */
            else if (error > 4 && error <= 12) { r->payload[chunk + 1] ^= (1 << (12 - error)); }
        }

        /* Decode the bytes */
        int bit_value;
        for (i = 0; i < first_data_in_byte_2; i++) {
            bit_value = get_bit(r->payload[chunk], data_bits[i]);
            if (bit_value) decoded_message[chunk / 2] |= (1 << (8 - i - 1));
            else decoded_message[chunk / 2] &= ~(1 << (8 - i - 1));
        }
        for (i = first_data_in_byte_2; i < 8; i++) {
            bit_value = get_bit(r->payload[chunk + 1], data_bits[i]);
            if (bit_value) decoded_message[chunk / 2] |= (1 << (8 - i - 1));
            else decoded_message[chunk / 2] &= ~(1 << (8 - i - 1));
        }
    }

    /* Set the new length and memcpy in r->payload the decoded message */
    r->len /= 2;
    memcpy(r->payload, decoded_message, r->len);

    return 1;
}

int encode(msg* t)
{
    char encoded_message[t->len * 2];
    int i, chunk, bit_value, j;
    int control_bit0, control_bit1, control_bit2, control_bit3;
    char byte1, byte2;

    /* Take each byte and encode it using hamming coding method */
    /* At the end, 1 byte becomes 2 bytes */
    for (chunk = 0, j = 0; chunk < t->len; chunk++, j += 2) {
        
        /* Init byte1 and byte2 */
        for (i = 0; i < 8; i++) {
            byte1 &= ~(1 << i);
            byte2 &= ~(1 << i);
        }

        /* Fill the 2nd byte with data bits */
        for (i = 0; i < 4; i++) {
            bit_value = get_bit(t->payload[chunk], i);
            if (bit_value) byte2 |= (1 << i);
            else byte2 &= ~(1 << i);
        }
        for (i = 4; i < 7; i++) {
            bit_value = get_bit(t->payload[chunk], i);
            if (bit_value) byte2 |= (1 << (i + 1));
            else byte2 &= ~(1 << (i + 1));
        }

        /* Fill the 1st byte with data bits */
        bit_value = get_bit(t->payload[chunk], 7);
        if (bit_value) byte1 |= (1 << 1);
        else byte1 &= ~(1 << 1);

        /* Calculate control bits and insert them into bytes1 and byte2 */
        control_bit0 = control_bit1 = control_bit2 = control_bit3 = 0;

        /* Compute control_bit0 */ 
        for (i = 0; i < first_c0_in_byte_2; i++) {
            control_bit0 += get_bit(byte1, c0_bit[i]);
        }
        for (i = first_c0_in_byte_2; i < 6; i++) {
            control_bit0 += get_bit(byte2, c0_bit[i]);
        }

        /* Compute control_bit1 */
        for (i = 0; i < first_c1_in_byte_2; i++) {
            control_bit1 += get_bit(byte1, c1_bit[i]);
        }
        for (i = first_c1_in_byte_2; i < 6; i++) {
            control_bit1 += get_bit(byte2, c1_bit[i]);
        }

        /* Compute control_bit2 */
        for (i = 0; i < first_c2_in_byte_2; i++) {
            control_bit2 += get_bit(byte1, c2_bit[i]);
        }
        for (i = first_c2_in_byte_2; i < 5; i++) {
            control_bit2 += get_bit(byte2, c2_bit[i]);
        }

        /* Compute control_bit3 */
        for (i = first_c3_in_byte_2; i < 5; i++) {
            control_bit3 += get_bit(byte2, c3_bit[i]);
        }

        control_bit0 %= 2; control_bit1 %= 2;
        control_bit2 %= 2; control_bit3 %= 2;

        if (control_bit0) byte1 |= (1 << 3);
        else byte1 &= ~(1 << 3);

        if (control_bit1) byte1 |= (1 << 2);
        else byte1 &= ~(1 << 2);

        if (control_bit2) byte1 |= (1 << 0);
        else byte1 &= ~(1 << 0);

        if (control_bit3) byte2 |= (1 << 4);
        else byte2 &= ~(1 << 4);

        encoded_message[chunk * 2] = byte1; encoded_message[chunk * 2 + 1] = byte2;
    }

    /* Set the new length and memcpy the encoded message */
    t->len = t->len * 2;
    memcpy(t->payload, encoded_message, t->len);

    return 1;
}

int execute_ls(char *argument, int mode)
{
    msg t, r;
    int res;

    /* Send confirmation for been receiving the command */
    sprintf(t.payload, ACK);
    if (mode == HAMMING) t.len = strlen(t.payload);
    else t.len = strlen(t.payload) + 1;
    res = send_message(&t);
    if (res < 0) {
        perror("[SERVER] Send ACK error. Exiting.\n");
        return -1;
    }  

    DIR *dir;
    struct dirent *file_s;

    /* Open directory */
    if ((dir = opendir(argument)) == NULL) {
        perror("[SERVER] Cannot open file\n");
        return -1;
    }

    /* Start reading the dir and counting files */
    int number_of_files = 0;
    do {
        errno = 0;
        if ((file_s = readdir(dir)) != NULL) {
            number_of_files++;
        }
    } while (file_s != NULL);

    if (errno != 0) {
        perror("[SERVER] Error while reading the dir\n");
        return -1;
    }

    /* Send a package containing the number of files found in the argument dir */
    if (mode == PARITY) {
        sprintf(t.payload + 1, "%d", number_of_files);
        t.len = strlen(t.payload);
        /* Get the parity of bytes starting with byte 1 */
        int parity = get_parity(t.payload, 1, t.len);
        /* Set the parity on the rightmost bit on the 0 byte */
        set_parity(t.payload, 0, parity);
    } else if (mode == NORMAL) {
        sprintf(t.payload, "%d", number_of_files);
        t.len = strlen(t.payload) + 1; 
    } else if (mode == HAMMING) {
        sprintf(t.payload, "%d", number_of_files);
        t.len = strlen(t.payload);
        encode(&t);
    }

    res = send_message(&t);
    if (res < 0) {
        perror("[SERVER] Send number of files found error\n");
        return -1;
    }

    /* Receive confirmation for receiving number_of_files */
    res = recv_message(&r);
    if (res < 0) {
        perror("[SERVER] Error, confirmation for nb_of_files not received\n");
        return -1;
    } else {
        // printf("[SERVER] Received %s from client\n", r.payload);
        if (mode == PARITY) {
            /* In case the client sended NACK, send the package over and over again */
            wait_until_ack(&r, &t);
        }
    }

    /* Walk through the directory again and send files names */
    rewinddir(dir);
    do {
        if ((file_s = readdir(dir)) != NULL) {
            
            /* Send current file name */
            if (mode == PARITY) {
                sprintf(t.payload + 1, file_s->d_name);
                t.len = strlen(file_s->d_name) + 1;
                /* Get the parity of bytes starting with byte 1 */
                int parity = get_parity(t.payload, 1, t.len);
                /* Set the parity on the rightmost bit on the 0 byte */
                set_parity(t.payload, 0, parity);
            } else if (mode == NORMAL || mode == HAMMING) {
                sprintf(t.payload, file_s->d_name);
                t.len = strlen(t.payload);
            }

            if (mode == HAMMING) encode(&t);

            res = send_message(&t);
            if (res < 0) {
                perror("[SERVER] Error while sending current filename\n");
                return -1;
            }

            /* Receive confirmation for the last file sent */
            res = recv_message(&r);
            if (res < 0) {
                perror("[SERVER] Error while receiving confirmation\n");
                return -1;
            } else {
                if (mode == PARITY) {
                    /* In case the client sended NACK, send the package over and over again */
                    wait_until_ack(&r, &t);
                }
            }
        }
    } while (file_s != NULL);

    closedir(dir);

    return 1;
}

int execute_cd(char *argument) 
{
    msg t;
    int res;

    /* Send confirmation for receiving the command */
    sprintf(t.payload, ACK);
    t.len = strlen(t.payload) + 1;
    res = send_message(&t);
    if (res < 0) {
        perror("[SERVER] Send ACK error. Exiting.\n");
        return -1;
    }

    int ret = chdir(argument);
    if (ret < 0) {
        perror("[SERVER] Failed to change dir");
        return -1;
    }

    return 1;
}

int execute_cp(char *argument, int mode) 
{
    msg t, r;
    int res;

    /* Send confirmation for receiving the command */
    sprintf(t.payload, ACK);
    t.len = strlen(ACK) + 1;
    res = send_message(&t);
    if (res < 0) {
        perror("[SERVER] Send ACK error. Exiting.\n");
        return -1;
    }

    /* Open the file received as a parameter */
    FILE* f = fopen(argument, "r");

    /* Determine the length of the file */
    fseek(f, 0L, SEEK_END);
    int file_length = ftell(f);
    fseek(f, 0L, SEEK_SET);

    /* Send a package containing the length of the file */
    if (mode == PARITY) {
        sprintf(t.payload + 1, "%d", file_length);
        t.len = strlen(t.payload);
        /* Get the parity of bytes starting with byte 1 */
        int parity = get_parity(t.payload, 1, t.len);
        /* Set the parity on the rightmost bit on the 0 byte */
        set_parity(t.payload, 0, parity);
    } else if (mode == NORMAL || mode == HAMMING) {
        sprintf(t.payload, "%d", file_length);
        t.len = strlen(t.payload);
        if (mode == HAMMING) encode(&t);
    }
    
    res = send_message(&t);
    if (res < 0) {
        perror("[SERVER] Error while sending file length. Exiting.\n");
        return -1;
    }

    /* Wait for the confirmation that file_length was receieved */
    res = recv_message(&r);
    if (res < 0) {
        perror("[SERVER] Error while receiving confirmation\n");
        return -1;
    } else {
        if (mode == PARITY) wait_until_ack(&r, &t);
    }

    /* Start sending pieces of data from the file.
       Each and every package should be <= 1400 bytes in size */
    int number_of_packages;
    if (mode == PARITY) {
        number_of_packages = file_length / (MSGSIZE - 1);
        if (file_length % (MSGSIZE - 1)) number_of_packages++;
    } else if (mode == NORMAL) {
        number_of_packages = file_length / MSGSIZE;
        if (file_length % MSGSIZE) number_of_packages++;
    } else if (mode == HAMMING) {
        number_of_packages = file_length / (MSGSIZE / 2);
        if (file_length % (MSGSIZE / 2)) number_of_packages++;
    }
    
    int package, objects_read;
    for (package = 1; package <= number_of_packages; package++) {
        /* Read the chunk of data */
        if (mode == PARITY) {
            objects_read = fread(t.payload + 1, sizeof(char), MSGSIZE - 1, f);
            t.len = objects_read + 1;
            /* Get the parity of bytes starting with byte 1 */
            int parity = get_parity(t.payload, 1, t.len);
            /* Set the parity on the rightmost bit on the 0 byte */
            set_parity(t.payload, 0, parity);
        } else if (mode == NORMAL) {
            objects_read = fread(t.payload, sizeof(char), MSGSIZE, f);
            t.len = objects_read;
        } else if (mode == HAMMING) {
            objects_read = fread(t.payload, sizeof(char), MSGSIZE / 2, f);
            t.len = objects_read;
            encode(&t);
        }

        res = send_message(&t);

        if (res < 0) {
            perror("[SERVER] Failed to send one chunk of data\n");
            return -1;
        }

        /* Wait for the confirmation that chunk of data was received by the client*/
        res = recv_message(&r);
        if (res < 0) {
            perror("[SERVER] Error while receiving confirmation\n");
            return -1;
        } else {
            // printf("[SERVER] Received %s from client\n", r.payload);
            if (mode == PARITY) wait_until_ack(&r, &t);
        }
    }

    /* Close the file */
    fclose(f);

    return 1;
}

int execute_sn(char* argument, int mode) 
{
    msg t, r;
    int res;

    /* Send confirmation for receiving the command */
    sprintf(t.payload, ACK);
    t.len = strlen(ACK) + 1;
    res = send_message(&t);
    if (res < 0) {
        perror("[SERVER] Send ACK error. Exiting.\n");
        return -1;
    }

    /* Create the file sent as an argument */
    char filename[256];
    strcpy(filename, "new_");
    strcat(filename, argument);
    FILE* f = fopen(filename, "w");

    /* Receive the package with the data length to write in the file */
    res = recv_message(&r);
    if (res < 0) {
        perror("[SERVER] Receive length of file to write error\n");
        return -1;
    } else {
        if (mode == PARITY) {
            /* While the received package has not the right parity, we take care of that */
            if (!is_parity_correct(r)) {
                wait_until_correct_parity(&r, &t);
            }
        } else if (mode == HAMMING) {
            detect_correct_errors_and_decode(&r);
        }
    }

    int c;
    int file_length = 0;
    if (mode == PARITY) {
        for (c = 1; c < r.len - 1; c++) {
            file_length = file_length * 10 + (r.payload[c] - '0');
        }
    } else if (mode == NORMAL || mode == HAMMING) {
        for (c = 0; c < r.len - 1; c++) {
            file_length = file_length * 10 + (r.payload[c] - '0');
        }
    }

    /* Send confirmation for receiving the file_length */
    sprintf(t.payload, ACK);
    t.len = strlen(ACK) + 1;
    res = send_message(&t);
    if (res < 0) {
        perror("[SERVER] Send ACK error. Exiting.\n");
        return -1;
    }

    /* Receive chunks of data and write them into the new created file */
    int number_of_packages;
    if (mode == PARITY) {
        number_of_packages = file_length / (MSGSIZE - 1);
        if (file_length % (MSGSIZE - 1)) number_of_packages++;   
    } else if (mode == NORMAL) {
        number_of_packages = file_length / MSGSIZE;
        if (file_length % MSGSIZE) number_of_packages++;   
    } else if (mode == HAMMING) {
        number_of_packages = file_length / (MSGSIZE / 2);
        if (file_length % (MSGSIZE / 2)) number_of_packages++; 
    }
         
    int package;
    for (package = 1; package <= number_of_packages; package++) {
        res = recv_message(&r);
        if (res < 0) {
            perror("[SERVER] Error while receiving chunk of data\n");
            return -1;
        } else {
            if (mode == PARITY) {
                /* While the received package has not the right parity, we take care of that */
                if (!is_parity_correct(r)) {                    
                    wait_until_correct_parity(&r, &t);
                }
            } else if (mode == HAMMING) detect_correct_errors_and_decode(&r);
        }

        /* Effectively write the data in the file */
        int objects_written;
        if (mode == PARITY) objects_written = fwrite(r.payload + 1, sizeof(char), r.len - 1, f);
        else if (mode == NORMAL || mode == HAMMING) {
            objects_written = fwrite(r.payload, sizeof(char), r.len, f);
        }

        /* Consider the two possible cases */
        if (mode == PARITY && objects_written == r.len - 1) {
            /* Send confirmation that data was written successfully */
            sprintf(t.payload, ACK);
            t.len = strlen(ACK) + 1;
            res = send_message(&t);
            if (res < 0) {
                perror("[SERVER] Send ACK error. Exiting.\n");
                return -1;
            }
        } else if ((mode == NORMAL && objects_written == r.len) || 
                (mode == HAMMING && objects_written == r.len)) {
            /* Send confirmation that data was written successfully */
            sprintf(t.payload, ACK);
            t.len = strlen(ACK);
            res = send_message(&t);
            if (res < 0) {
                perror("[SERVER] Send ACK error. Exiting.\n");
                return -1;
            }
        } else {
            printf("[SERVER] Failed to write entire chunk %d of data in the file\n", package);
        }
    }

    /* Close the file */
    fclose(f);

    return 1;
}

int execute_exit(char *argument, int mode)
{
    msg t;
    int res;

    /* Send confirmation for received the command */
    sprintf(t.payload, ACK);
    if (mode == PARITY) t.len = strlen(t.payload) + 1;
    else if (mode == NORMAL || HAMMING) t.len = strlen(t.payload);
    res = send_message(&t);
    if (res < 0) {
        perror("[SERVER] Send ACK error. Exiting.\n");
        return -1;
    }

    return 1;
}

int running_mode(int mode)
{
    msg r, t;
    int res;

    int current_state = 0;

    while (current_state != END_TRANSMISSION) {

        /* Receive a package from client */
        res = recv_message(&r);
        if (res < 0) {
            perror("[SERVER] Receive error. Exiting\n");
            return -1;
        }

        if (mode == PARITY) {
            /* While the received package has not the right parity, we take care of that */
            if (!is_parity_correct(r)) wait_until_correct_parity(&r, &t);
        } else if (mode == HAMMING) {
            detect_correct_errors_and_decode(&r);
        }

        /* Split package that contains client's want */
        char* command;
        char* argument;
        char* token;
        char* separator = " ";

        /* Get the command */
        if (mode == PARITY) token = strtok(r.payload + 1, separator);
        else if (mode == NORMAL || mode == HAMMING) token = strtok(r.payload, separator);
        command = strdup(token);

        /* Get the argument */
        token = strtok(NULL, separator);
        argument = strdup(token);

        /* Figure out the type of command */
        if (!strcmp(LS, command)) {
            if (!execute_ls(argument, mode)) {
                printf("[SERVER] Command LS executed unsuccessufully\n");
            }
        } else if (!strcmp(CD, command)) {
            if (!execute_cd(argument)) {
                printf("[SERVER] Command CD executed unsuccessufully\n");
            }
        } else if (!strcmp(CP, command)) {
            if (!execute_cp(argument, mode)) {
                printf("[SERVER] Command CP executed unsuccessufully\n");
            }
        } else if (!strcmp(SN, command)) {
            if (!execute_sn(argument, mode)) {
                printf("[SERVER] Command SN executed unsuccessufully\n");
            }
        } else if (!strcmp(EXIT, command)) {
            if (!execute_exit(argument, mode)) {
                printf("[SERVER] Command EXIT executed unsuccessufully\n");
            }
            current_state = -1;
        } else {
            printf("[SERVER] Received unknown command. Exiting.\n");
            current_state = -1;
        }
    }

    return EXITED_NORMALLY;

}

int main(int argc, char** argv)
{    
    printf("[RECEIVER] Starting.\n");
    init(HOST, PORT);

    // Determine running mode
    if (argc > 1) {
        if (!strcmp(argv[1], RUN_PARITY_MODE)) {
            running_mode(PARITY);
        } else if (!strcmp(argv[1], RUN_HAMMING_MODE)) {
            running_mode(HAMMING);
        } else { 
            printf("[SERVER] Running unknown mode. Exiting.\n");
            return 0;
        }
    } else {
        running_mode(NORMAL);
    }

    printf("[RECEIVER] Finished receiving..\n");

    return 0;
}
