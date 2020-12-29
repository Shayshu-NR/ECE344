#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "wc.h"

//Added function
unsigned long hash_function(char *str, int max_table);
struct wc_item *create_new_word_item(char *key);
void wc_item_destroy(struct wc_item *item);
void collision_handler(struct wc *table, struct wc_item *collision_word, unsigned long index);

//Each word is given a key, value pair
//In this case the key is the word and
//the value is the number of times it's been seen!
//It also acts as a linked list to tie together collided words
struct wc_item {
    char *key;
    int repetition;
    struct wc_item *next;
};


//We need to point to an array
//Of our items and also have the
//size of the array (count) and the
//size of the hash table
struct wc {
    struct wc_item **word_index;
    int count;
    long size;
};


struct wc *wc_init(char *word_array, long size) {
    struct wc *wc;

    //Allocate memory for the hash table
    wc = (struct wc *) malloc(sizeof(struct wc));
    assert(wc);

    //Allocate memory for the entire hash table array and overflow list and initialize the parameters
    //of wc
    wc->word_index = (struct wc_item **) malloc(size * sizeof(struct wc_item *));
    wc->count = 0;
    wc->size = size;

    //Now initialize the items in wc, first set them all to null...
    for (int i = 0; i < wc->size; i++) {
        wc->word_index[i] = NULL;
    }

    //Now go through the word array and add all the words
    int i = 0;
    int j = 0;
    int word_size = 0;
    char *new_word = NULL;

    while (word_array[i] != '\0') {
        if (isspace(word_array[i]) != 0) {

            //Make sure that the word isn't a /n or /t character
            if (word_size != 0) {
                new_word = (char *) malloc((word_size + 1) * sizeof(char));

                //Fill new word with the characters from word_array
                while (j < word_size) {
                    new_word[j] = word_array[(i - word_size) + j];
                    j++;
                }
                //Make it a string!
                new_word[j] = '\0';

                //Now check if the word already exists
                unsigned long index = hash_function(new_word, size);

                //If the spot is empty then create a new item and insert it directly
                if (wc->word_index[index] == NULL) {
                    wc->word_index[index] = create_new_word_item(new_word);
                    wc->count++;
                } else {
                    //Check if the words are the same...
                    //If they are then increase the repetition counter
                    if (strcmp(new_word, wc->word_index[index]->key) == 0) {
                        wc->word_index[index]->repetition++;
                    }
                    //Otherwise we know that a collision has occurred!
                    else {
                        struct wc_item *collision_word = create_new_word_item(new_word);
                        collision_handler(wc, collision_word, index);
                        wc->count++;
                    }
                }

                free(new_word);
            }

            word_size = 0;
            j = 0;
        } else {
            word_size++;
        }
        i++;
    }

    return wc;
}


void wc_output(struct wc *wc) {
    //Just go through the hash array and output the word and the repetitions...
    for (int i = 0; i < wc->size; i++) {
        if (wc->word_index[i] != NULL) {
            struct wc_item *iterator = wc->word_index[i];
            while (iterator != NULL) {
                printf("%s:%d\n", iterator->key, iterator->repetition);
                iterator = iterator->next;
            }
        }
    }

    return;
}


void wc_destroy(struct wc *wc) {
    //Free all the wc_items and overflow list items
    for (int i = 0; i < wc->size; i++) {
        if (wc->word_index[i] != NULL) {
            wc_item_destroy(wc->word_index[i]);
        }
    }

    free(wc->word_index);
    free(wc);

    return;
}


//This function DESTROYS (!) a wc_item recursively
void wc_item_destroy(struct wc_item *item) {

    //Because it's bascially a linked list just get to the end of the chain
    //then start freeing all the elements
    if(item->next != NULL){
        wc_item_destroy(item->next);
    }
    free(item->key);
    free(item);

    return;
}


//This function creates a wc_item which is used to store key, value, and repetition
//We want to return a pointer to a word item,
//In which we store the key, value and number of
//repetitions of the word in question...
struct wc_item *create_new_word_item(char *key) {
    //Create the wc_item and allocate memory
    struct wc_item *new_word_item = (struct wc_item *) malloc(sizeof(struct wc_item));

    //Allocate memory for the key
    new_word_item->key = (char *) malloc((strlen(key) + 1)*sizeof(char));

    //Finally copy the key and value into the item structure
    strcpy(new_word_item->key, key);
    new_word_item->repetition = 1;

    //Lastly set the next item to nothing!
    new_word_item->next = NULL;

    return new_word_item;
}


//This hash function takes in a word and finds the index for where it belongs
//Found online here: http://www.cse.yorku.ca/~oz/hash.html
unsigned long hash_function(char *str, int max_table) {
    unsigned long hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
        /* hash * 33 + c */
    }

    return hash % max_table;
}


//If we have a collision then insert a new link into the list...
void collision_handler(struct wc *table, struct wc_item *collision_word, unsigned long index) {
    //Go through the list of wc_items till the end or until
    //we find a matching key
    char *search_key = collision_word->key;

    struct wc_item *iterator = table->word_index[index];
    while (strcmp(search_key, iterator->key) != 0 && iterator->next != NULL) {
        iterator = iterator->next;
    }

    //Now we've found the search key or found the end of the list...
    if (strcmp(search_key, iterator->key) == 0) {
        iterator->repetition++;
        wc_item_destroy(collision_word);
    } else if (iterator->next == NULL) {
        iterator->next = collision_word;
    }

    return;
}