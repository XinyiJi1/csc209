#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "helper.h"
#include <getopt.h>
#include <string.h>

//the merge function for the parent process
int merge(struct rec* min_list, FILE* f2, int n){
       int flag = 0;
       //the index of the list who need to add a new one into
       int add = 0;
       struct rec min;
       for(int k = 0; k < n; k++){
            if(min_list[k].freq != -2){
                if(flag == 0){
                    min = min_list[k];
                    add = k;
                    flag = 1;
                }else if(min.freq > min_list[k].freq){
                    min = min_list[k];
                    add = k;
                }
            }
        }
        //write the rec into the output file
        if (fwrite(&min, sizeof(struct rec),1,f2) != 1){
             fprintf(stderr, "Error: data not fully written to file\n");
             exit(1);
        }
        return add;
}

//the sort function for child process
struct rec* sort(char* infile, int current,int whether){
    struct rec* compare_list;
    if((compare_list= malloc(whether*sizeof(struct rec))) == NULL){
        perror("malloc");
        exit(1);
    }
    FILE* f1 = fopen(infile, "rb");
    if (f1 == NULL) {
        perror("fopen\n");
        exit(1);
    }
    fseek(f1, (current)*sizeof(struct rec), SEEK_SET);              
    //read the part this child need to read
    for(int i = 0; i < whether; i++){
        if(fread(&compare_list[i], sizeof(struct rec),1 ,f1)==0){
            fprintf(stderr,"fread failed\n");
            exit(1);
        }
    }
    //sort array using qsort functions
    qsort(compare_list, whether, sizeof(struct rec), compare_freq);
    //close the input file
    if(fclose(f1) != 0){
        perror("fclose\n");
        exit(1);
    }
    return compare_list;
}


int main(int argc, char *argv[]) {   
    char *infile;
    char *outfile;
    //the number of the process
    int n;
    //for output file
    FILE *f2;
    //if the incorrect number of command-line arguments 
    //or incorrect options are provided,report that using the 
    //message and exit the program with an exit code of 1
    if(argc != 7){
        fprintf(stderr, "Usage: psort -n <number of processes> -f <inputfile> -o <outputfile>\n");
        exit(1);
    }
    //getopt part for detect incorrect input
    int opt;
    while((opt = getopt(argc, argv, "n:f:o:")) != -1){
        switch(opt)
        {
             case 'n':
                n = (int)strtol(optarg, NULL, 10);
                if(n <= 0){
                   n=1;
                }
               
                break;
             case 'f':
                infile = optarg;
                
                break;
             case 'o':
                outfile = optarg;
                
                break;
             default:
                fprintf(stderr, "Usage: psort -n <number of processes> -f <inputfile> -o <outputfile>\n");
                exit(1);
         }
    }
    /*if(optind < argc){
        fprintf(stderr, "Usage: psort -n <number of processes> -f <inputfile> -o <outputfile>\n");
        exit(1);
    }*/

    //struct rec r;
    //while(fread(&r, sizeof(struct rec), 1, f1) != 0){
    //    printf("%d %s\n", r.freq, r.word);
    //}

    int status;
    int pipe_fd[n][2];   

    //total element in the input file
    int sum;    
    sum = get_file_size(infile)/sizeof(struct rec);
    
    //check if number of process is bigger than the whole number of element
    if(n>sum){
        n = sum;
    }
    if(sum == 0){
        //open the output file
        f2 = fopen(outfile, "wb");
        if (f2 == NULL) {
            perror("fopen\n");
            exit(1);
        }  
        //close output file
        if(fclose(f2) != 0){
            perror("fclose\n");
            exit(1);
        } 
        exit(0);
    }
    // the number of element for each child
    int whether;
    
    int result = 1;
    int i = 1;
    int remainder = sum % n;
    //the element has been read
    int current = 0;
    //the number of the previous children
    int child_no;

    while(i <= n){
        if(result > 0){
            // call pipe before we fork
            if ((pipe(pipe_fd[i-1])) == -1) {
                perror("pipe");
                exit(1);
            }
            // call fork
            result = fork();
            if (result < 0) {
                perror("fork");
                exit(1);
            }else if(result == 0){
                  
                 // before we forked the parent had open the reading ends to
                 // all previously forked children -- so close those
                 for (child_no = 0; child_no < i-1; child_no++) {
                     if (close(pipe_fd[child_no][0]) == -1) {
                         perror("close reading ends of previously forked children");
                         exit(1);
                     }
                     
                  }
                  // child does their work here
                  // child only writes to the pipe so close reading end
                  if (close(pipe_fd[i-1][0]) == -1) {
                      perror("close reading end from inside child");
                      exit(1);
                  }
                  //give each child the element it has to read
                  if(i <= remainder){
                      whether = sum/n+1;
                  }else{
                      whether = sum/n;
                  }
                  
                  //get the sorted list of the element by call sort function
                  struct rec* compare_list = sort(infile,current,whether);
                  //write into pipe
                  for(int k = 0;k < whether;k++){
                     if (write(pipe_fd[i-1][1], &compare_list[k], sizeof(struct rec)) == -1){
                          perror("write from child to pipe");
                          exit(1);
                     } 
                  }
                  free(compare_list);                    
                  // I'm done with the pipe so close it
                  if (close(pipe_fd[i-1][1]) == -1) {
                      perror("close pipe after writing");
                      exit(1);
                  }
                  //just exit the child process
                  exit(0);
            }else{
                // in the parent but before doing the next loop iteration
                // close the end of the pipe that I don't want open
                if (close(pipe_fd[i-1][1]) == -1) {
                    perror("close writing end of pipe in parent");
                    exit(1);
                }
                //the number has been read
                current += sum/n;
                if(i <= remainder){
                    current++;
                }
            }
        }
        i++;
        
    }
    
    //open the output file
    f2 = fopen(outfile, "wb");
    if (f2 == NULL) {
        perror("fopen\n");
        exit(1);
    }
    //the number of the index of the list which need to add
    int add; 
    //the list containing all the smallest elements of the child
    struct rec min_list[n];
    //start merge!
    for(int i = 0;i < n;i++){
        if(read(pipe_fd[i][0], &(min_list[i]), sizeof(struct rec)) == 0){
            fprintf(stderr,"fail to read");
            exit(1);
        }
    }
    for(int i = 0;i < sum;i++){
        add=merge(min_list, f2,n);
        if(read(pipe_fd[add][0], &(min_list[add]), sizeof(struct rec)) == 0){
            min_list[add].freq = -2;
        }
    } 
    
    //wait for children ending successfully
    for (int t = 0; t < n; t++) {
        if (wait(&status) == -1) {
            perror("wait");
            exit(1);
        }
        if (WIFEXITED(status) == 0) {
            fprintf(stderr, "Child terminated abnormally\n");
        }
    }
    
    //close output file
    if(fclose(f2) != 0){
        perror("fclose\n");
        exit(1);
    }

    return 0;
}


