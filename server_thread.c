#include "request.h"
#include "server_thread.h"
#include "common.h"
#include <pthread.h>
#include <stdbool.h>

//~~~~~ Added Functions ~~~~~
void stub_function(struct server *sv);

struct wc_item
{
	// Hash key
	char *key;

	//Actual file data
	struct file_data *data;

	// Reference bit
	int ref_bit;

	// Pointer to next collision item because we're using
	// chaining
	struct wc_item *next;
	struct wc_item *lru_next;

	pthread_cond_t *done_sending;
};
struct head_of_lru
{
	struct wc_item *head;
};
struct wc
{
	struct wc_item **files;
	long count;
	long size;
	struct head_of_lru *lru_queue;
};

unsigned long hash_function(char *str, int max_table);
int collision_handler(struct server *sv, struct wc *table, struct wc_item *collision_word, unsigned long index);
void wc_item_destroy(struct server *sv, struct wc_item *item);
struct wc_item *cache_lookup(struct server *sv, char *file_name);
void cache_insert(struct server *sv, struct file_data *new_file);
int cache_add(struct server *sv, struct file_data *data);
int cache_evict(struct server *sv, struct wc_item *new_file, int index);
void cache_delete(struct server *sv, struct wc_item *to_be_deleted, int index);
void maintain_lru(struct head_of_lru *lru_queue, struct wc_item *lru, bool new_file);
struct wc_item *least_recently_used_file(struct head_of_lru *lru_queue);
void print_cache(struct wc *wc);
void print_lru(struct head_of_lru *lru_queue);
//~~~~~~~~~~~~~~~~~~~~~~~~~~~

struct server
{
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;

	// Fill this circular buffer in when processing a request
	int *buffer;

	// The cache for storing files
	struct wc *cache;

	// Circular buffer tracker similar to procuder consumer question
	// These need to be implemented using mutex and conditional variables
	int in;
	int out;

	// Array of worker threads
	pthread_t **workers;

	// Conditional variables used to signal that the
	// buffer is either full or empty
	pthread_cond_t *full;
	pthread_cond_t *empty;

	// Conditional variable used to signal that
	// a file is done being sent
	pthread_cond_t *in_use;

	// Mutex lock for critical sections
	pthread_mutex_t *mutex_lock;

	// Mutex lock for checking the cache
	pthread_mutex_t *mutex_cache_lock;
};

/* static functions */

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
	//printf("Do server request conffd: %d\n", connfd);
	int ret;
	int success = 0;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq)
	{
		file_data_free(data);
		return;
	}

	if (sv->max_cache_size > 0)
	{
		pthread_mutex_lock(sv->mutex_cache_lock);
		//printf("Cache look up for: %s\n", data->file_name);
		struct wc_item *cached_file = cache_lookup(sv, data->file_name);
		if (cached_file != NULL)
		{
			// Marked the cached file as in use...
			cached_file->ref_bit++;

			// If the file was found in cache then update data...
			data->file_name = cached_file->data->file_name;
			data->file_buf = cached_file->data->file_buf;
			data->file_size = cached_file->data->file_size;
			request_set_data(rq, data);
		}
		else
		{
			pthread_mutex_unlock(sv->mutex_cache_lock);
			/* read file if you coulnd't find it in the cache, 
			* fills data->file_buf with the file contents,
			* data->file_size with file size. */
			ret = request_readfile(rq);
			if (ret == 0)
			{ /* couldn't read file */
				goto out;
			}
			pthread_mutex_lock(sv->mutex_cache_lock);

			// Try adding the file to the cache
			success = cache_add(sv, data);
			if (success == 1)
			{
				int index = hash_function(data->file_name, sv->max_cache_size);
				cached_file = sv->cache->files[index];
				cached_file->ref_bit++;
			}
			// The file was found while trying to add it to the cache
			else if (success == 2)
			{
				int index = hash_function(data->file_name, sv->max_cache_size);
				cached_file = sv->cache->files[index];

				// Toi compensate for chaining
				while (cached_file->next != NULL && strcmp(cached_file->key, data->file_name) != 0)
				{
					cached_file = cached_file->next;
				}
				cached_file->ref_bit++;
			}
		}

		// print_cache(sv->cache);
		// print_lru(sv->cache->lru_queue);
		pthread_mutex_unlock(sv->mutex_cache_lock);

		/* send file to client */
		request_sendfile(rq);

		// Tell everyone that you're done sending the file...
		if (cached_file != NULL)
		{
			pthread_mutex_lock(sv->mutex_cache_lock);
			cached_file->ref_bit--;

			// Only tell one thread to continue with eviction..
			if (cached_file->ref_bit == 0)
			{
				//printf("Done sending %s\n", cached_file->key);
				pthread_cond_broadcast(cached_file->done_sending);
			}
			pthread_mutex_unlock(sv->mutex_cache_lock);
		}
		else if (success == -1)
		{
			file_data_free(data);
		}
	}
	else
	{
		/* read file if you coulnd't find it in the cache, 
			* fills data->file_buf with the file contents,
			* data->file_size with file size. */
		ret = request_readfile(rq);
		if (ret == 0)
		{ /* couldn't read file */
			goto out;
		}

		/* send file to client */
		request_sendfile(rq);
	}
out:
	request_destroy(rq);
	if (sv->max_cache_size == 0)
	{
		file_data_free(data);
	}
}

/* entry point functions */

struct server *server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests + 1;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;

	// Init the mutex locks and signals
	// Use NULL to init because IDK what attributes to actually use
	sv->mutex_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	sv->mutex_cache_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	sv->full = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	sv->empty = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	sv->in_use = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	pthread_cond_init(sv->in_use, NULL);
	pthread_mutex_init(sv->mutex_lock, NULL);
	pthread_mutex_init(sv->mutex_cache_lock, NULL);
	pthread_cond_init(sv->full, NULL);
	pthread_cond_init(sv->empty, NULL);
	sv->in = 0;
	sv->out = 0;

	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0)
	{
		// Buffer used to store requests
		if (max_requests > 0)
		{
			sv->buffer = (int *)malloc((max_requests + 1) * (sizeof(int)));
		}

		// Buffer is filled for worker threads to read...

		// Create nr_threads
		// Allocate space for the array of threads
		if (nr_threads > 0)
		{
			sv->workers = (pthread_t **)malloc(sizeof(pthread_t *) * nr_threads);
			for (int i = 0; i < nr_threads; i++)
			{
				//printf("Creating thread %d\n", i);

				// Allocate space for each individual thread
				sv->workers[i] = (pthread_t *)malloc(sizeof(pthread_t));

				// Init each thread by having them go to the stub function until a request
				// has been parsed where they can then send all the info...
				pthread_create(sv->workers[i], NULL, (void *)&stub_function, sv);
			}
		}

		// Create the cache
		// Allocate max_cache_size worth of space
		if (max_cache_size > 0)
		{
			sv->cache = (struct wc *)malloc(sizeof(struct wc));

			// Array for storing all the files
			sv->cache->files = (struct wc_item **)malloc(sizeof(struct wc_item *) * max_cache_size);
			for (int i = 0; i < max_cache_size; i++)
			{
				sv->cache->files[i] = NULL;
			}

			sv->cache->count = 0;
			sv->cache->size = max_cache_size;
			sv->cache->lru_queue = (struct head_of_lru *)malloc(sizeof(struct head_of_lru));
			sv->cache->lru_queue->head = NULL;
		}
	}

	return sv;
}

void server_request(struct server *sv, int connfd)
{
	// //printf("Server request connfd: %d\n", connfd);

	if (sv->nr_threads == 0)
	{ /* no worker threads */
		do_server_request(sv, connfd);
	}
	else
	{
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */

		// Update the buffer and update, also lock now
		// because this stuff is global ya feel?
		pthread_mutex_lock(sv->mutex_lock);
		// //printf("Obtained lock in server_request\n");

		// Wait for a signal while the buffer is full
		// This signal comes from the worker thread stub function
		// [Request1 | Request2 | Request3 ... | RequestMax]
		//		^								      ^
		//		|									  |
		//	   out									  in
		while ((sv->in - sv->out + sv->max_requests) % sv->max_requests == sv->max_requests - 1 && sv->exiting != 1)
		{
			// Waiting on signal from stub_function
			// //printf("In server_request waiting for sv->full\n");
			pthread_cond_wait(sv->full, sv->mutex_lock);
		}
		// //printf("Not waiting on sv->full\n");

		// Update the buffer with connfd
		// So now a worker thread will read this value and then do the request
		sv->buffer[sv->in] = connfd;

		// Unblock threads waiting on sv->empty
		// Tell the threads waiting on the buffer to be NOT empty that there is a request availible
		if ((sv->in - sv->out + sv->max_requests) % sv->max_requests == 0)
		{
			// //printf("Sending signal sv->empty\n");
			pthread_cond_broadcast(sv->empty);
		}

		// Increment the in pointer, recall the buffer is circular
		sv->in = (sv->in + 1) % sv->max_requests;
		// //printf("In: %d\n", sv->in);

		// //printf("Releasing lock in request_server\n");
		// Release the lock
		pthread_mutex_unlock(sv->mutex_lock);
	}
}

void server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	sv->exiting = 1;
	//printf("Sent exit command to threads!\n");

	// Tell all threads to leave the worker loop
	pthread_cond_broadcast(sv->empty);

	// Join all threads
	if (sv->nr_threads > 0)
	{

		for (int i = 0; i < sv->nr_threads; i++)
		{
			pthread_join(*sv->workers[i], NULL);
		}
	}

	/* make sure to free any allocated resources */
	if (sv->max_requests - 1 > 0)
	{
		free(sv->buffer);
	}

	// Free all the thread workers
	if (sv->nr_threads > 0)
	{
		for (int i = 0; i < sv->nr_threads; i++)
		{
			free(sv->workers[i]);
		}
		free(sv->workers);
	}

	// Free the cache...
	if (sv->max_cache_size > 0)
	{

		for (int i = 0; i < sv->max_cache_size; i++)
		{
			if (sv->cache->files[i] != NULL)
			{
				wc_item_destroy(sv, sv->cache->files[i]);
			}
		}
		free(sv->cache->files);
	}

	// Free locks and monitors...
	free(sv->mutex_lock);
	free(sv->full);
	free(sv->empty);

	free(sv);
}

//~~~~~ Implemented added functions ~~~~~

// Have worker threads chill out here (blocked) until
// the master thread signals for one of them to handle a request
void stub_function(struct server *sv)
{
	// //printf("Thread arrived at stub!\n");
	// Similar to the producer consumer problem, have threads wait until
	// the buffer has an open spot before serving the request
	while (true)
	{
		// Obtain the lock...
		pthread_mutex_lock(sv->mutex_lock);
		// //printf("Obtained lock in stub_function\n");

		// Wait for a signal while the buffer is empty
		// Meaning that there are no more requests to be filled
		// In server_request signal to a waiting thread that there is a new request (pthread_signal)
		while ((sv->in - sv->out + sv->max_requests) % sv->max_requests == 0 && sv->exiting != 1)
		{
			// //printf("In stub waiting on sv->empty\n");
			pthread_cond_wait(sv->empty, sv->mutex_lock);
		}

		int current_request = sv->buffer[sv->out];

		// We know that the buffer is not empty
		// However, if the buffer is full then then we have to make
		// sure that those requests get processed...
		// Let other threads know that the buffer is full
		if ((sv->in - sv->out + sv->max_requests) % sv->max_requests == sv->max_requests - 1)
		{
			// This goes to server_request...
			// //printf("Sending signal sv->full\n");
			pthread_cond_signal(sv->full);
		}

		// Now that there is a request
		// update the out index...
		// Recall that the buffer is circular!
		sv->out = (sv->out + 1) % sv->max_requests;
		// //printf("Out: %d\n", sv->out);

		// Release the lock
		// //printf("Releasing lock in stub_function\n");
		pthread_mutex_unlock(sv->mutex_lock);

		// When the server calls sv->exiting then exit the thread
		if (sv->exiting == 1)
		{
			// //printf("Exiting thread!\n");
			pthread_exit(NULL);
		}

		// Finnaly do the actual request...
		do_server_request(sv, current_request);
	}
}

//This hash function takes in a word and finds the index for where it belongs
//Found online here: http://www.cse.yorku.ca/~oz/hash.html
unsigned long hash_function(char *str, int max_table)
{
	unsigned long hash = 5381;
	int c;

	//printf("Hashing for %s\n", str);
	while ((c = *str++))
	{
		hash = ((hash << 5) + hash) + c;
		/* hash * 33 + c */
	}

	return hash % max_table;
}

//If we have a collision then insert a new link into the list...
int collision_handler(struct server *sv, struct wc *table, struct wc_item *collision_word, unsigned long index)
{
	//Go through the list of wc_items till the end or until
	//we find a matching key
	char *search_key = collision_word->key;

	struct wc_item *iterator = table->files[index];
	while (iterator->next != NULL && strcmp(search_key, iterator->key) != 0)
	{
		iterator = iterator->next;
	}

	//Now we've found the search key or found the end of the list...
	if (strcmp(search_key, iterator->key) == 0)
	{
		return -1;
	}
	else if (iterator->next == NULL)
	{
		iterator->next = collision_word;
	}

	return 1;
}

//This function DESTROYS (!) a wc_item recursively
void wc_item_destroy(struct server *sv, struct wc_item *item)
{
	//Because it's bascially a linked list just get to the end of the chain
	//then start freeing all the elements
	if (item->next != NULL)
	{
		wc_item_destroy(sv, item->next);
	}

	// Want to make sure
	sv->cache->count -= item->data->file_size;
	free(item->key);
	file_data_free(item->data);
	free(item);
	return;
}

// Look up a file in the cache
struct wc_item *cache_lookup(struct server *sv, char *file_name)
{
	// Get the index into the cache array
	unsigned long index = hash_function(file_name, sv->max_cache_size);
	//printf("Hash index: %ld\n", index);

	// Because we're using chaining go through a potential linked list
	// until the correct file name is found
	struct wc_item *iterator = sv->cache->files[index];
	if (iterator != NULL)
	{
		while (iterator->next != NULL && strcmp(iterator->key, file_name) != 0)
		{
			//printf("SEARCHING FOR KEY\n");
			iterator = iterator->next;
		}
	}

	if (iterator != NULL && strcmp(iterator->key, file_name) == 0)
	{
		//printf("Found file in cache! Key: %s, File: %s\n", iterator->key, file_name);

		// Check if the file might be corrupted...
		if (iterator->lru_next != NULL && strncmp("./fileset_dir/", iterator->lru_next->key, strlen("./fileset_dir/")) != 0)
		{
			//printf("Corrupted!\n");
			struct wc_item *temp = iterator->lru_next;

			if (temp->data != NULL)
			{
				file_data_free(temp->data);
			}
			free(temp->key);
			free(temp);
			temp = NULL;

			iterator->lru_next = NULL;
		}

		// Update the lru
		maintain_lru(sv->cache->lru_queue, iterator, false);
		return iterator;
	}
	else
	{
		//printf("Not found in cache!\n");
		return NULL;
	}
}

// Try adding the file to the cache
int cache_add(struct server *sv, struct file_data *data)
{
	// We know that the file isn't in the cache
	unsigned long index = hash_function(data->file_name, sv->max_cache_size);

	// For now simply insert it...
	struct wc_item *new_file = (struct wc_item *)malloc(sizeof(struct wc_item));
	new_file->key = (char *)malloc((strlen(data->file_name) + 1) * sizeof(char));
	strcpy(new_file->key, data->file_name);
	new_file->data = (struct file_data *)malloc(sizeof(struct file_data));
	new_file->data = data;
	new_file->next = NULL;
	new_file->lru_next = NULL;
	new_file->ref_bit = 0;
	new_file->done_sending = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	pthread_cond_init(new_file->done_sending, NULL);

	// Make sure that the cache has enough space
	// if it does not then evict a file...
	if (sv->cache->files[index] == NULL && (sv->cache->count + new_file->data->file_size) < sv->max_cache_size)
	{
		//printf("EZ insertion: %s\n", new_file->data->file_name);

		// Add the file the hash table...
		sv->cache->files[index] = (struct wc_item *)malloc(sizeof(struct wc_item));
		sv->cache->files[index] = new_file;
		sv->cache->count += new_file->data->file_size;

		// Update the lru...
		maintain_lru(sv->cache->lru_queue, new_file, true);
		return 1;
	}
	else if (sv->cache->files[index] != NULL && (sv->cache->count + new_file->data->file_size) < sv->max_cache_size)
	{
		//printf("Collision insertion! %s\n", new_file->data->file_name);

		// Add the file to the hash table via chaining...
		if (collision_handler(sv, sv->cache, new_file, index) == 1)
		{
			// Update the lru...
			maintain_lru(sv->cache->lru_queue, new_file, true);
			sv->cache->count += new_file->data->file_size;
		}

		return 1;
	}
	else
	{
		//Evict!
		//printf("Evict a page! %s\n", new_file->key);
		int evict_status = cache_evict(sv, new_file, index);

		// Eviction was successful
		if (evict_status == 1)
		{
			if (sv->cache->files[index] == NULL)
			{

				//printf("EZ insertion: %s\n", new_file->data->file_name);

				// Add the file the hash table...
				sv->cache->files[index] = (struct wc_item *)malloc(sizeof(struct wc_item));
				sv->cache->files[index] = new_file;
				sv->cache->count += new_file->data->file_size;
			}
			else
			{
				//printf("Collision insertion! %s\n", new_file->data->file_name);

				// Add the file to the hash table via chaining...
				if (collision_handler(sv, sv->cache, new_file, index) == 1)
				{
					// Update the lru...
					maintain_lru(sv->cache->lru_queue, new_file, true);
					sv->cache->count += new_file->data->file_size;
				}
			}

			//printf("Successfully evicted and inserted!\n");
			maintain_lru(sv->cache->lru_queue, new_file, true);
			return 1;
		}
		// Eviction failed...
		else if (evict_status == -1)
		{
			// free(new_file->key);
			// free(new_file);
			return -1;
		}
		// The file was found while you were trying to evict stuff
		else
		{
			free(new_file->key);
			free(new_file->done_sending);
			free(new_file);
			return 2;
		}
	}
}

//Evict an item from the cache
int cache_evict(struct server *sv, struct wc_item *new_file, int index)
{
	// Check if new_file got placed into the HT
	if (sv->cache->files[index] != NULL && strcmp(sv->cache->files[index]->key, new_file->key) == 0)
	{
		//printf("Already inserted!\n");
		return 2;
	}

	//Find the least recently used file and evict it
	//If the file is currently in use then wait for the file to be done being used
	if (new_file->data->file_size > sv->max_cache_size)
	{
		// Make sure to free the new_file
		//printf("File too big\n");
		return -1;
	}
	else
	{
		// Remove the least recently used file, and update the current size of the cache
		// If there isn't enough space then DO IT AGAIN!

		// Get the LRU file
		struct wc_item *to_be_evicted = sv->cache->lru_queue->head;
		struct wc_item *new_end = NULL;
		int retry = 0;
		if (to_be_evicted != NULL)
		{
			while (to_be_evicted->lru_next != NULL)
			{
				new_end = to_be_evicted;
				to_be_evicted = to_be_evicted->lru_next;
			}

			// If the file is currently in use then wait for it to be done...
			//printf("To be evicted [%s]\n", to_be_evicted->key);
			int n = to_be_evicted->ref_bit;
			while (n > 0)
			{
				//printf("%s in use!\n", to_be_evicted->key);
				pthread_cond_wait(to_be_evicted->done_sending, sv->mutex_cache_lock);
				n = to_be_evicted->ref_bit;
				retry++;
			}

			// If another thread already evicted to_be_evicted then retry...
			if (retry > 0)
			{
				// Check if another thread already added the file...
				if (sv->cache->files[index] == NULL)
				{
					//printf("Retry!\n");
					return cache_evict(sv, new_file, index);
				}
				// Otherwise it has already been serviced so leave
				else
				{
					return -1;
				}
			}
			// Otherwise delete that file and check if another needs to be deleted...
			else
			{
				if (new_end != NULL)
				{
					new_end->lru_next = to_be_evicted->lru_next;
				}
				else
				{
					// Front of lru just got evicted...
					sv->cache->lru_queue->head = NULL;
				}

				cache_delete(sv, to_be_evicted, hash_function(to_be_evicted->data->file_name, sv->max_cache_size));

				// If the cache is still not big enough then evict another item...
				if ((sv->max_cache_size - sv->cache->count) < new_file->data->file_size)
				{
					return cache_evict(sv, new_file, index);
				}
				// If the cache is big enough then insert new_file into the hash table....
				else
				{
					return 1;
				}
			}
		}
	}

	//printf("Failed to_evicted != NULL\n");
	return -1;
}

// Delete a file from the cache!
void cache_delete(struct server *sv, struct wc_item *to_be_deleted, int index)
{
	//printf("Deleting file!\n");
	struct wc_item *iterator = sv->cache->files[index];
	struct wc_item *previous = NULL;

	// If the item is chained due to collisions find the previous and the actual
	// pointer to to_be_deleted
	while (iterator->next != NULL && strcmp(iterator->key, to_be_deleted->key) != 0)
	{
		previous = iterator;
		iterator = iterator->next;
	}

	// Handle chaining...
	if (previous != NULL)
	{
		previous->next = to_be_deleted->next;
	}

	// Now free to_be_deleted...
	sv->cache->count -= to_be_deleted->data->file_size;
	sv->cache->files[index] = to_be_deleted->next;
	free(to_be_deleted->key);
	file_data_free(to_be_deleted->data);

	if (previous == NULL && to_be_deleted->next == NULL)
	{
		free(sv->cache->files[index]);
		sv->cache->files[index] = NULL;
		assert(sv->cache->files[index] == NULL);
	}
	else
	{
		to_be_deleted->next = NULL;
		to_be_deleted->lru_next = NULL;
		free(to_be_deleted);
	}
	return;
}

// Rearrange the lru to keep track of which file is the most recently used
void maintain_lru(struct head_of_lru *lru_queue, struct wc_item *lru, bool new_file)
{

	struct wc_item *head = lru_queue->head;
	// New list
	if (head == NULL)
	{
		lru_queue->head = lru;
		lru->lru_next = NULL;
		return;
	}

	// Move lru to the front of the queue, then rearrange the queue

	// When has already been in the list...
	// lru -> [KeyM] -> [Key(M+1)] ... -> [KeyN] -> END
	// HEAD -> [Key1] -> [Key2] -> ... [KeyN] -> END
	// HEAD -> [KeyM] -> [Key1] -> [Key2] -> ... [KeyN] -> END
	if (lru->lru_next != NULL && !new_file)
	{
		// Check if lru is at the fron of the list...
		if (strcmp(head->key, lru->key) == 0)
		{
			//Do nothing
			return;
		}

		// Find the previous node
		//printf("Middle maintain %s\n", lru->key);
		struct wc_item *iterator = head;
		while (iterator->lru_next != NULL && strcmp(iterator->lru_next->key, lru->key) != 0)
		{
			iterator = iterator->lru_next;
		}

		iterator->lru_next = lru->lru_next;
		lru->lru_next = head;
		lru_queue->head = lru;
		return;
	}
	// When lru is a newly added element or end of the list...
	else
	{
		// If the file is new then it's pretty simple...
		// lru -> [KeyM] -> END
		// HEAD -> [Key1] -> [Key2] -> ... [KeyN] -> END
		// HEAD -> [KeyM] -> [Key1] -> [Key2] -> ... [KeyN] -> END
		if (new_file)
		{
			//printf("New file maintain\n");
			lru->lru_next = head;
			lru_queue->head = lru;
			return;
		}
		// Otherwise the file is at the end of the list...
		// lru -> [KeyM] -> END
		// HEAD -> [Key1] -> [Key2] -> ... [KeyM] -> END
		// HEAD -> [KeyM] -> [Key1] -> [Key2] -> ... [Key(M-1)] -> END
		else
		{

			// Edge case where the list is one element long
			// Do nothing
			// HEAD - > [Key1] -> END
			//printf("End case maintain\n");
			if (strcmp(head->key, lru->key) == 0)
			{
				lru->lru_next = NULL;
				return;
			}

			struct wc_item *iterator = head;
			struct wc_item *previous = NULL;
			while (iterator->lru_next != NULL && strcmp(iterator->lru_next->key, lru->key) != 0)
			{
				previous = iterator;
				iterator = iterator->lru_next;
			}

			if (previous != NULL)
			{
				iterator->lru_next = lru->lru_next;
				lru->lru_next = head;
				lru_queue->head = lru;
			}
			// Two item linked list edge case....
			else
			{
				lru_queue->head = lru;
			}
		}
	}

	return;
}

// Print the cache for debugging
void print_cache(struct wc *wc)
{
	//Just go through the hash array and output the word and the repetitions...
	int cache_size = 0;
	//printf("{\n");
	for (int i = 0; i < wc->size; i++)
	{
		if (wc->files[i] != NULL)
		{
			struct wc_item *iterator = wc->files[i];
			while (iterator != NULL)
			{
				//printf("(%d) %s : %d,\n", i, iterator->key, iterator->data->file_size);
				cache_size += iterator->data->file_size;
				iterator = iterator->next;
			}
		}
	}
	//printf("\n}");
	//printf("\nCache Capacity: %ld\nCurrent Size: %ld\n", wc->size, wc->count);
	return;
}

// Print the LRU queue
void print_lru(struct head_of_lru *lru_queue)
{
	//printf("\nPrint LRU...\n");
	//printf("Head ->\n");
	struct wc_item *iterator = lru_queue->head;

	while (iterator != NULL)
	{
		//printf("[%s] ->\n", iterator->key);
		iterator = iterator->lru_next;
	}
	//printf("[NULL]\n\n");
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~