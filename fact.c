#include "common.h"
#include <stdio.h>
#include <regex.h>

regex_t regex;
int result;

int factorial(int n) {
    //n! = (n)(n-1)(n-2)...(2)(1)

    if (n <= 1) {
        return 1;
    } else {
        return n * factorial(n - 1);
    }
}

int main(int argc, char **argv) {

    //Make sure an input was given
    if (argc > 1) {
        //int regcomp(regex_t *preg, const char *pattern, int cflags);
        result = regcomp(&regex, "[a-zA-Z[:punct:]]", 0);

        //int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags);
        result = regexec(&regex, argv[1], 0, NULL, 0);

        //If result == false then we have a match else no matches
        if (!result) {
            printf("Huh?\n");
            return 0;
        } else if (result == REG_NOMATCH) {
            //That means the argument is just numbers
            //So now make sure it's less than 12

            //Conver the pointer to an integer
            char *argument;
            long converter = strtol(argv[1], &argument, 10);
            int number = converter;

            //Now perform the checks
            if(number == 0){
                printf("Huh?\n");
                return 0;
            }
            else if (number <= 12) {
                printf("%d\n", factorial(number));
                return 0;
            } else {
                printf("Overflow\n");
                return 0;
            }

        } else {
            printf("Huh?\n");
            return 0;
        }

        printf("Huh?\n");
        return 0;

    } else {
        printf("Huh?\n");
        return 0;
    }

    return 0;
}


