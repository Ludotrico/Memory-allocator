#include "helpers.h"
#include "icsmm.h"


/* Helper function definitions go here */


int calculateBlockSize(int size) {
    const int A = 16;
    return (((size+(A-1))/A)*A) + 16;
}

int getCeil(double n)
{
    int N=n;
    if(n==N)
        return N;
    return N+1;
}

ics_free_header* getFirstFreeBlock(ics_free_header* head, int requiredBlockSize) {
    while (head != NULL) {
        if (head->header.block_size >= requiredBlockSize)
            return head;
        head = head->next;
    }
    return NULL;
}

void insert(ics_free_header** head, ics_free_header* header) {
    debug("%s\n", " ");
    header->next = *head;
    header->prev = NULL;
    debug("%s\n", " ");

    if (*head != NULL)
        (*head)->prev = header;
    debug("%s\n", " ");
    *head = header;
    debug("%s\n", " ");
}

void removeNode(ics_free_header** head, ics_free_header* header) {
    debug("header %p, next %p, prev %p\n", header,header->next,header->prev);
    if ( (header->prev == NULL) && (header->next == NULL)) {
        //Case node is the only one in the list
        *head = NULL;
    }
    else if (header->prev == NULL) {
        //Case node is at head
        *head = header->next;
        header->next->prev = NULL;
    } else if (header->next == NULL) {
        //Case node is at end
        header->prev->next = NULL;
    } else {
        //Case node is in middle
        header->next->prev = header->prev;
        header->prev->next = header->next;
    }

    header->next = NULL;
    header->prev = NULL;
    debug("%s\n", " ");
}


void splinter(ics_free_header ** head, ics_header* startOfBlock, int fullBlockSize, int requiredBlockSize, int size) {
    
    int leftOverBytes = fullBlockSize - requiredBlockSize;
    bool isSplinter = leftOverBytes < 32;
    debug("leftOverBytes: %d\n", leftOverBytes);
    debug("startOfBlock1 %p\n", startOfBlock);
    // update header in allocated block
    (*((ics_header*)startOfBlock)).block_size = isSplinter ? fullBlockSize : requiredBlockSize;
    (*((ics_header*)startOfBlock)).block_size |= 1;
    (*((ics_header*)startOfBlock)).padding_amount = requiredBlockSize - (size+16);
    (*((ics_header*)startOfBlock)).hid = HEADER_MAGIC;
    

    //update footer in allocated block
    void * endOfBlock = ((char*)startOfBlock) + (requiredBlockSize-8);
    debug("endOfBlock1 %p\n", endOfBlock);
    (*((ics_footer*)endOfBlock)).fid = FOOTER_MAGIC;
    (*((ics_footer*)endOfBlock)).block_size = isSplinter ? fullBlockSize : requiredBlockSize;
    (*((ics_footer*)endOfBlock)).block_size |= 1;

    

    if (!isSplinter) {
        // Case leftover block is not splinter, add it to free list
        // add left over to free list
        // set header and insert in free list
        void * startOfBlock2 = (char*)endOfBlock + 8;
        debug("startOfBlock2 %p\n", startOfBlock2);
        (*((ics_free_header*)startOfBlock2)).header.block_size = leftOverBytes;
        (*((ics_free_header*)startOfBlock2)).header.padding_amount = 0;
        (*((ics_free_header*)startOfBlock2)).header.hid = HEADER_MAGIC;
        insert(&(*head), (ics_free_header*)startOfBlock2);
        
    
        // set footer
        void * endOfBlock2 = ((char*)startOfBlock2) + (leftOverBytes-8);
        debug("end of BLock2 %p\n", endOfBlock2);
        (*((ics_footer*)endOfBlock2)).fid = FOOTER_MAGIC;
        debug("%s\n", " ");
        (*((ics_footer*)endOfBlock2)).block_size = leftOverBytes;
    }

}



int validatePtr(ics_header * header, void* prologue, void* epilogue, int allocBit) {
    do {
        debug("header %p\n",header);
        // header is bewteen prologue and epilogue
        if ( ((void*)header < prologue)  || ((void*)header > epilogue)   )
            break;

        debug("%s\n", "Checking allocation");

        // allocated bit is set in header
        if ( (header->block_size & 1) != allocBit    )
            break;

        debug("header block size %d\n", header->block_size);
        
        ics_footer * footer = ((ics_footer*) (((char*)header) + ((header->block_size&0xfffe)-8)));
        debug("footer %p\n", footer);
        // footer is bewteen prologue and epilogue
        if ( ((void*)footer < prologue)  || ((void*)footer > epilogue)   )
            break;

        // allocated bit is set in footer
        if ( (footer->block_size & 1) != allocBit    )
            break;

        // header/footer ID is correct

        debug("%s\n", "Checking header");
        if ( header->hid != HEADER_MAGIC) 
            break;

        debug("%s\n", "Checking footer");
        if ( footer->fid != FOOTER_MAGIC) 
            break;

        debug("Checking size h: %d f: %d\n", header->block_size, footer->block_size);
        // header/footer block size is equal
        if ( header->block_size != footer->block_size)
            break;

        return 1;           // all good
    } while(0);

    debug("%s\n", " ");
    errno = EINVAL;
    return 0;
}

void * coalesceUp(ics_free_header ** head,  ics_header * startOfBlock, void* prologue, void* epilogue) {
    
    ics_header * nextHeader = ((ics_header*) (((char*)startOfBlock) + (startOfBlock->block_size & 0xFFFE)));
    debug("nextHeader %p\n",nextHeader);

    //Validate next block
    if (!validatePtr(nextHeader, prologue, epilogue, 0)) 
        return NULL;
    ics_footer * nextFooter = ((ics_footer*) (((char*)nextHeader) + ((nextHeader->block_size & 0xFFFE)-8)));
    debug("nextFooter %p\n",nextFooter);


    debug("\n");
    removeNode(&(*head), (ics_free_header*) nextHeader);

    debug("header %p\n",startOfBlock);
    startOfBlock->block_size = (startOfBlock->block_size & 0xFFFE) + (nextHeader->block_size & 0xFFFE);
    startOfBlock->block_size |= 1;
    startOfBlock->hid = HEADER_MAGIC;
    debug("header coalesced size %d\n",startOfBlock->block_size);

    //ics_footer* footer = ((ics_footer*) (((char*)startOfBlock) + ((startOfBlock->block_size & 0xFFFE)-8)));
    debug("footer %p\n",nextFooter);
    nextFooter->block_size = (startOfBlock->block_size & 0xFFFE);
    nextFooter->block_size |= 1;
    nextFooter->fid = FOOTER_MAGIC;
    debug("coalesced size %d\n",nextFooter->block_size);

    return startOfBlock;
        
}

void * coalesceDown(ics_free_header ** head,  ics_header * startOfBlock, void* prologue, void* epilogue) {
    ics_footer * pastFooter = ((ics_footer*) (((char*)startOfBlock) -8));
    debug("pastFooter %p\n",pastFooter);

    ics_header * pastHeader = ((ics_header*) (((char*)pastFooter) - ((pastFooter->block_size & 0xFFFE)-8)));
    debug("pastHeader %p\n",pastHeader);

    //Validate past block
    if (!validatePtr(pastHeader, prologue, epilogue, 0)) 
        return NULL;
    
    debug("\n");
    removeNode(&(*head), (ics_free_header*) pastHeader);
    char * newFooter = ((char*)startOfBlock) + ((startOfBlock->block_size & 0xFFFE)-8);
    debug("footer %p\n",newFooter);
    (*((ics_footer*) newFooter)).block_size = (startOfBlock->block_size & 0xFFFE) + (pastHeader->block_size & 0xFFFE);
    (*((ics_footer*) newFooter)).block_size |= 1;
    (*((ics_footer*) newFooter)).fid = FOOTER_MAGIC;

    debug("footer coalesced size %d\n",(*((ics_footer*) newFooter)).block_size);

    pastHeader->block_size = (*((ics_footer*) newFooter)).block_size;
    pastHeader->block_size |= 1;
    pastHeader->hid = HEADER_MAGIC;
    debug("header coalesced size %d\n",pastHeader->block_size);

    return pastHeader;
}