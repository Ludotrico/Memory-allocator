/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 * If you want to make helper functions, put them in helpers.c
 */
#include "icsmm.h"
#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>



ics_free_header *freelist_head = NULL;

//Global variables
static int mallocCount = 0;
void * PROLOGUE = NULL;
void * EPILOGUE = NULL;

void *ics_malloc(size_t size) { 
    //Check input
    if (size == 0) {
        errno = EINVAL;
        return NULL;
    }
    int isFirstMalloc = (mallocCount == 0);
    if ( (isFirstMalloc && (size > ((6*PAGE_SIZE)-32)))) {
        errno = ENOMEM;
        return NULL;
    }
    debug("%s\n", " ");

    //MALLOC
    // get first free block (if one exists)
    int requiredBlockSize = calculateBlockSize(size);
    debug("size %d -> %d\n",(int)size,requiredBlockSize);
    ics_free_header* freeBlock = getFirstFreeBlock(freelist_head, requiredBlockSize);
    debug("free block %p\n", freeBlock);

    if (freeBlock == NULL) {
        //No free blocks found, request more pages
        int pagesNeeded = getCeil((double)size/(PAGE_SIZE-32));
        int pageBlockSize = pagesNeeded*PAGE_SIZE;
        debug("pagesNeeded %d, pageBlockSize=%d\n",pagesNeeded,pageBlockSize);
        void * startPage = ics_inc_brk(pagesNeeded);
        debug("start of the page %p\n",startPage);

        if (((intptr_t)startPage) == -1) {
            //Insufficient memory
            errno = ENOMEM;
            return NULL;
        }

        if (isFirstMalloc) {
            //First malloc, set prologue
            PROLOGUE = startPage;
            //Lose 16 for prologue and epilogue
            pageBlockSize -= 16;
            debug("Setting prologue to 1\n");
            (*((ics_footer*)startPage)).block_size = 1;
        } else {
            //Epilogue of prev page becomes free +8 , therefore you only loose -8 for the new epilogue
            pageBlockSize -= 0;
        }
        debug("Setting the epilogue\n");

        // set epilogue
        EPILOGUE = (((char*)ics_get_brk())-8);
        (*((ics_header*)  EPILOGUE  )).block_size = 1;
        (*((ics_header*)EPILOGUE)).padding_amount = 0;
        debug("PROLOGUE %p EPILOGUE %p\n", PROLOGUE, EPILOGUE);

        // set header and insert in free list
        void * startOfBlock = isFirstMalloc ? ((char*)startPage + 8) : ((char*)startPage - 8);
        debug("StartOfBlock %p\n",startOfBlock);
        (*((ics_free_header*)startOfBlock)).header.block_size = pageBlockSize;
        (*((ics_free_header*)startOfBlock)).header.padding_amount = 0;
        (*((ics_free_header*)startOfBlock)).header.hid = HEADER_MAGIC;
        debug("%s\n", " ");
        insert(&freelist_head, (ics_free_header*)startOfBlock);
        debug("%s\n", " ");

        // set footer
        void * endOfBlock = ((char*)EPILOGUE) -8;
        debug("EndofBlock %p\n", endOfBlock);
        (*((ics_footer*)endOfBlock)).fid = FOOTER_MAGIC;
        (*((ics_footer*)endOfBlock)).block_size = pageBlockSize;
        debug("%s\n", " ");

        freeBlock = (ics_free_header*)startOfBlock;

        if (!isFirstMalloc) {
           //Coalesce down
            if (COALESCEDOWN) {
                freeBlock = (ics_free_header*) coalesceDown(&freelist_head, (ics_header*)startOfBlock, PROLOGUE, EPILOGUE);
                if (freeBlock == NULL) 
                    freeBlock = (ics_free_header*)startOfBlock;
            }
        }
    }
    debug("%s\n", " ");

    //Take freeBlock out of linked list 
    removeNode(&freelist_head, (ics_free_header*)freeBlock);

    // Allocate and check for splinter   
    debug("%s\n", " ");
    splinter(&freelist_head, (ics_header*) freeBlock, freeBlock->header.block_size, requiredBlockSize, size);
    debug("%s\n", " ");


    mallocCount++;
    debug("malloc(%d)-->%p\n", (int)size,((char*)freeBlock + 8));
    return ((char*)freeBlock + 8); 
}

void *ics_realloc(void *ptr, size_t size) {

    debug("realloc(%p, %d)\n",ptr,(int)size);
    //Validate ptr
    ics_header* startOfBlock = ((ics_header*)  (((char*)ptr - 8)));
    debug("StartOfBlock %p\n",startOfBlock);
    if (!validatePtr(startOfBlock, PROLOGUE, EPILOGUE, 1)) {
        return NULL;
    }

    int requiredBlockSize = calculateBlockSize(size);
    debug("required %d\n",requiredBlockSize);
    //Always coalesce (update header/footer and remove coalesced block from list)
    //Coalesce up
    ics_header* newHeader = NULL;
    if (COALESCEUP) {
        newHeader =  (ics_header*) coalesceUp(&freelist_head, startOfBlock, PROLOGUE, EPILOGUE);
        if (newHeader == NULL) 
            newHeader = startOfBlock;
    }

    //Coalesce down
    if (COALESCEDOWN) {
        newHeader = (ics_header*) coalesceDown(&freelist_head, startOfBlock, PROLOGUE, EPILOGUE);
        if (newHeader == NULL) 
            newHeader = startOfBlock;
    }


    int coalescedBlockSize = (newHeader->block_size & 0xFFFE);
    debug("coalesced %d\n",coalescedBlockSize);


    debug("coalesced %d\n",coalescedBlockSize);
    if (size == 0) {
        //Free coalesced block
        ics_free(ptr);
        return NULL;
    } else if (requiredBlockSize == coalescedBlockSize) {
        //Return coalesced block
        return ptr;
    } else if (requiredBlockSize < coalescedBlockSize) {
        if ( (coalescedBlockSize - requiredBlockSize) > 32 ) {
            //Case it is not a splinter
            debug("%s","splinter case\n");
            splinter(&freelist_head, newHeader, coalescedBlockSize, requiredBlockSize, size);
        }
        return ptr;
    } else if (requiredBlockSize > coalescedBlockSize) {
        //Find another free block
        void* newBlock = ics_malloc(size);
        if (newBlock == NULL) {
            //Malloc failed
            return NULL;
        }

        debug("%s","new block case\n");
        //Copy payload into newBlock
        char* originalPayload = ptr;
        char* newPayload = newBlock;
        for (int i = 0; i < (requiredBlockSize - 16); i++) {
            *newPayload = *originalPayload;

            originalPayload++;
            newPayload++;
        }
        debug("%s","done copying\n");
        ics_free(ptr);
        return newBlock;

    }
    return NULL; 
}

int ics_free(void *ptr) { 
    debug("free(%p)\n",ptr);
    ics_header* startOfBlock = ((ics_header*)  (((char*)ptr - 8)));
    debug("startOfBlock %p\n",startOfBlock);
    if (!validatePtr(startOfBlock, PROLOGUE, EPILOGUE, 1)) {
        debug("%s","validate failed\n");
        return -1;
    }

    //Coalesce up
    ics_header* newHeader = NULL;
    if (COALESCEUP) {
        newHeader =  (ics_header*) coalesceUp(&freelist_head, startOfBlock, PROLOGUE, EPILOGUE);
        if (newHeader == NULL) 
            newHeader = startOfBlock;
    }

    //Coalesce down
    if (COALESCEDOWN) {
        newHeader = (ics_header*) coalesceDown(&freelist_head, startOfBlock, PROLOGUE, EPILOGUE);
        if (newHeader == NULL) 
            newHeader = startOfBlock;
    }


    int coalescedBlockSize = (newHeader->block_size & 0xFFFE);
    debug("coalesced %d\n",coalescedBlockSize);

    // set header and insert in free list
    (*((ics_free_header*)newHeader)).header.block_size = coalescedBlockSize;
    (*((ics_free_header*)newHeader)).header.padding_amount = 0;
    (*((ics_free_header*)newHeader)).header.hid = HEADER_MAGIC;
    insert(&freelist_head, (ics_free_header*)newHeader);

    // set footer
    ics_footer* newFooter = (ics_footer*) (((char*)newHeader) + (newHeader->block_size -8));
    (*((ics_footer*)newFooter)).block_size = coalescedBlockSize;
    (*((ics_footer*)newFooter)).fid = FOOTER_MAGIC;

    return 0; 
}
