#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Reads a trace file produced by valgrind and an address marker file produced
 * by the program being traced. Outputs only the memory reference lines in
 * between the two markers
 */

int main(int argc, char **argv) {
    
    if(argc != 3) {
         fprintf(stderr, "Usage: %s tracefile markerfile\n", argv[0]);
         exit(1);
    }

    // Addresses should be stored in unsigned long variables
    // unsigned long start_marker, end_marker;
    unsigned long start_marker;
    unsigned long end_marker;
    FILE * fp2;
    FILE * fp1;
    fp2=fopen(argv[2],"r");
    fscanf(fp2, "%lx %lx", &start_marker, &end_marker);
    fclose(fp2);

    unsigned long current;
    char type;
    int store;
    fp1=fopen(argv[1],"r");
    while(fscanf(fp1," %c %lx,%d",&type,&current,&store) != EOF && current!=start_marker){
    }
    while(fscanf(fp1," %c %lx,%d",&type,&current,&store) != EOF && current!=end_marker){
       	printf("%c,%#lx\n",type,current);
    }
    fclose(fp1);
    


    /* For printing output, use this exact formatting string where the
     * first conversion is for the type of memory reference, and the second
     * is the address
     */
    // printf("%c,%#lx\n", VARIABLES TO PRINT GO HERE);

    return 0;
}
