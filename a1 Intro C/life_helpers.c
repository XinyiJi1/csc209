#include <stdio.h>


void print_state(char *state, int size){
	for(int i=0;i<size;i++){
		printf("%c",state[i]);
	}
	printf("\n");
}

void update_state(char *state, int size){
	char before=state[0];
	int i=1;
	char after;
	while (i<size-1){
		after=state[i+1];
		if(before==after){
			before=state[i];
			state[i]='.';
		}else{
			before=state[i];
			state[i]='X';
		}
		i++;
	}
}
