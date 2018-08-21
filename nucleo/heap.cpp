// heap.cpp
// sram dtcm 128k  0x20000000 0x00020000
// sram axi  512k  0x24000000 0x00080000
// sram 1    128k  0x30000000 0x00020000
// sram 2    128k  0x30020000 0x00020000
// sram 3     32k  0x30040000 0x00008000
// sram 4     64k  0x30080000 0x00010000
//{{{  includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "heap.h"
//}}}

#define MIN_ALLOC_SZ 4
#define BIN_COUNT 9
#define BIN_MAX_IDX (BIN_COUNT - 1)

typedef unsigned int uint;

//{{{  c style heap
//{{{  struct node_t
typedef struct node_t {
  bool hole;
  uint size;
  struct node_t* next;
  struct node_t* prev;
  } node_t;
//}}}
//{{{  struct footer_t
typedef struct {
  node_t* header;
  } footer_t;
//}}}
//{{{  struct bin_t
typedef struct {
  node_t* head;
  } bin_t;
//}}}
//{{{  struct heap_t
typedef struct {
  long start;
  long end;
  long size;
  long free;
  long minFree;
  bin_t* bins[BIN_COUNT];
  } heap_t;
//}}}

//{{{
node_t* get_best_fit (bin_t* bin, size_t size) {

  if (bin->head == NULL)
    return NULL; // empty list!

  node_t *temp = bin->head;
  while (temp != NULL) {
    if (temp->size >= size)
      return temp; // found a fit!
    temp = temp->next;
    }

  return NULL; // no fit!
  }
//}}}
//{{{
node_t* get_last_node (bin_t* bin) {

  node_t* temp = bin->head;
  while (temp->next != NULL)
    temp = temp->next;
  return temp;
  }
//}}}
footer_t* get_foot (node_t *node) { return (footer_t*)((char*)node + sizeof(node_t) + node->size); }
//{{{
void createFoot (node_t *head) {
  footer_t* foot = get_foot (head);
  foot->header = head;
  }
//}}}
//{{{
int get_bin_index (size_t sz) {

  int index = 0;
  sz = sz < 4 ? 4 : sz;

  while (sz >>= 1)
    index++;
  index -= 2;

  if (index > BIN_MAX_IDX)
    index = BIN_MAX_IDX;
  return index;
  }
//}}}
//{{{
void addNode (bin_t* bin, node_t* node) {

  node->next = NULL;
  node->prev = NULL;

  node_t* temp = bin->head;

  if (bin->head == NULL) {
    bin->head = node;
    return;
    }

  // we need to save next and prev while we iterate
  node_t* current = bin->head;
  node_t* previous = NULL;

  // iterate until we get the the end of the list or we find a
  // node whose size is
  while (current != NULL && current->size <= node->size) {
    previous = current;
    current = current->next;
    }

  if (current == NULL) { // we reached the end of the list
    previous->next = node;
    node->prev = previous;
    }

  else {
    if (previous != NULL) { // middle of list, connect all links!
      node->next = current;
      previous->next = node;
      node->prev = previous;
      current->prev = node;
      }
    else { // head is the only element
      node->next = bin->head;
      bin->head->prev = node;
      bin->head = node;
      }
    }
  }
//}}}
//{{{
void removeNode (bin_t* bin, node_t* node) {

  if (bin->head == NULL)
    return;

  if (bin->head == node) {
    bin->head = bin->head->next;
    return;
    }

  node_t* temp = bin->head->next;
  while (temp != NULL) {
    if (temp == node) { // found the node
      if (temp->next == NULL) { // last item
        temp->prev->next = NULL;
        }
      else { // middle item
        temp->prev->next = temp->next;
        temp->next->prev = temp->prev;
        }
      // we dont worry about deleting the head here because we already checked that
      return;
      }
    temp = temp->next;
    }
  }
//}}}

//{{{
void init_heap (heap_t* heap, int start, int size) {

  for (int i = 0; i < BIN_COUNT; i++) {
    heap->bins[i] = (bin_t*)malloc (sizeof(bin_t));
    memset (heap->bins[i], 0, sizeof(bin_t));
    }

  // first we create the initial region, this is the "wilderness" chunk, heap starts as one big chunk of memory
  node_t* init_region = (node_t*)start;
  init_region->hole = true;
  init_region->size = (size) - sizeof(node_t) - sizeof(footer_t);

  createFoot (init_region); // create a foot (size must be defined)

  // now we add the region to the correct bin and setup the heap struct
  addNode (heap->bins[get_bin_index (init_region->size)], init_region);

  heap->start = start;
  heap->end = start + size;
  heap->size = size;
  heap->free = size;
  heap->minFree = size;
  }
//}}}
//{{{
void* heap_alloc (heap_t* heap, size_t size) {

  vTaskSuspendAll();

  // first get the bin index that this chunk size should be in
  int index = get_bin_index (size);

  // now use this bin to try and find a good fitting chunk!
  bin_t* temp = (bin_t*)heap->bins[index];
  node_t* found = get_best_fit (temp, size);

  // while no chunk if found advance through the bins until we find a chunk or get to the wilderness
  while (found == NULL) {
    temp = heap->bins[++index];
    found = get_best_fit (temp, size);
    }

  // if the differnce between the found chunk and the requested chunk is bigger than
  // overhead (metadata size) + the min alloc size then split chunk, otherwise just return the chunk
  if ((found->size - size) > (sizeof(footer_t) + sizeof(node_t) + MIN_ALLOC_SZ)) {
    // do the math to get where to split at, then set its metadata
    node_t* split = (node_t*)(((char*)found + sizeof(footer_t) + sizeof(node_t)) + size);
    split->size = found->size - size - (sizeof(footer_t) + sizeof(node_t));
    split->hole = true;

    createFoot (split); // create a footer for the split

    // now we need to get the new index for this split chunk place it in the correct bin
    int new_idx = get_bin_index (split->size);
    addNode (heap->bins[new_idx], split);

    found->size = size; // set the found chunks size
    createFoot (found); // since size changed, remake foot
    }

  found->hole = false; // not a hole anymore
  removeNode (heap->bins[index], found); // remove it from its bin

  // since we don't need the prev and next fields when the chunk
  // is in use by the user, we can clear these and return the address of the next field
  found->prev = NULL;
  found->next = NULL;
  heap->free -= size;
  if (heap->free < heap->minFree)
    heap->minFree = heap->free;

  xTaskResumeAll();

  return &found->next;
  }
                                                                             //}}}
//{{{
void heap_free (heap_t* heap, void* ptr) {

  vTaskSuspendAll();

  // the actual head of the node is not ptr, it is ptr minus the size of the fields that precede "next"
  // in the node structure, if the node being free is the start of the heap then there is
  // no need to coalesce so just put it in the right list
  node_t* head = (node_t*)((char*)ptr - (sizeof(int) * 2));
  heap->free += head->size;

  if ((long)head != heap->start) {
    // these are the next and previous nodes in the heap, not the prev and next
    // in a bin. to find prev we just get subtract from the start of the head node
    // to get the footer of the previous node (which gives us the header pointer).
    // to get the next node we simply get the footer and add the sizeof(footer_t).
    node_t* next = (node_t*)((char*) get_foot (head) + sizeof (footer_t));
    node_t* prev = (node_t*)*((int*)((char*)head - sizeof (footer_t)));

    // if the previous node is a hole we can coalese!
    if (prev->hole) {
      // remove the previous node from its bin
      bin_t* list = heap->bins[get_bin_index(prev->size)];
      removeNode (list, prev);

      // re-calculate the size of thie node and recreate a footer
      prev->size += sizeof(footer_t) + sizeof(node_t) + head->size;
      createFoot (prev);

      // previous is now the node we are working with, we head to prev
      // because the next if statement will coalesce with the next node
      // and we want that statement to work even when we coalesce with prev
      head = prev;
      }

    // if the next node is free coalesce!
    if (next->hole) {
      // remove it from its bin
      bin_t* list = heap->bins[get_bin_index(next->size)];
      removeNode (list, next);

      // re-calculate the new size of head
      head->size += sizeof(footer_t) + sizeof(node_t) + next->size;

      // clear out the old metadata from next
      footer_t* oldFooter = get_foot (next);
      oldFooter->header = 0;
      next->size = 0;
      next->hole = false;

      // make the new footer!
      createFoot (head);
      }
     }

  // this chunk is now a hole, so put it in the right bin!
  head->hole = true;
  addNode (heap->bins[get_bin_index (head->size)], head);

  xTaskResumeAll();
  }
//}}}
//}}}

//{{{
class cHeap {
public:
  //{{{
  cHeap (uint32_t start, size_t size, bool debug) : mDebug (debug) {

    // Ensure the heap starts on a correctly aligned boundary
    size_t uxAddress = (size_t)start;
    mSize = size;
    if ((uxAddress & mAlignmentMask) != 0) {
      uxAddress += (mAlignment - 1);
      uxAddress &= ~((size_t)mAlignmentMask);
      mSize -= uxAddress - (size_t)start;
      }

    uint8_t* pucAlignedHeap = (uint8_t*)uxAddress;

    // mStart is used to hold a pointer to the first item in the list of free blocks.
    mStart.mNextFreeBlock = (tLink_t*)pucAlignedHeap;
    mStart.mBlockSize = (size_t)0;

    // mEnd is used to mark the end of the list of free blocks and is inserted at the end of the heap space. */
    uxAddress = ((size_t)pucAlignedHeap) + mSize;
    uxAddress -= kHeapStructSize;
    uxAddress &= ~((size_t) mAlignmentMask );
    mEnd = (tLink_t*)uxAddress;
    mEnd->mBlockSize = 0;
    mEnd->mNextFreeBlock = NULL;

    // start with single free block sized to take up entire heap space, minus space taken by mEnd
    tLink_t* firstFreeBlock = (tLink_t*)pucAlignedHeap;
    firstFreeBlock->mBlockSize = uxAddress - (size_t)firstFreeBlock;
    firstFreeBlock->mNextFreeBlock = mEnd;

    // Only one block exists - and it covers the entire usable heap space
    mMinFreeBytesRemaining = firstFreeBlock->mBlockSize;
    mFreeBytesRemaining = firstFreeBlock->mBlockSize;
    }
  //}}}

  size_t getSize() { return mSize; }
  size_t getFree() { return mFreeBytesRemaining; }
  size_t getMinSize() { return mMinFreeBytesRemaining; }

  //{{{
  uint8_t* alloc (size_t size) {

    size_t largestBlock = 0;

    vTaskSuspendAll();
    uint8_t* allocAddress = allocBlock (size);
    xTaskResumeAll();

    if (mDebug) {
      printf ("cHeap::alloc size:%d free:%d minFree:%d largest:%d\n",
              size, mFreeBytesRemaining,mMinFreeBytesRemaining, largestBlock);

      tLink_t* block = mStart.mNextFreeBlock;
      while (block) {
        if ((block->mBlockSize & kBlockAllocatedBit) == 0)
          printf (" - alloc %p size:%d\n", block, block->mBlockSize);
        else
          printf (" -  free %p size:%d\n", block, block->mBlockSize);
        block = block->mNextFreeBlock;
        }
      }

    return allocAddress;
    }
  //}}}
  //{{{
  void free (void* ptr) {

    if (ptr) {
      // memory being freed will have an tLink_t structure immediately before it.
      uint8_t* puc = (uint8_t*)ptr - kHeapStructSize;
      tLink_t* link = (tLink_t*)puc;

      if (mDebug)
        printf ("cHeap::free %p %d\n", ptr, link->mBlockSize & (~kBlockAllocatedBit));

      if (link->mBlockSize & kBlockAllocatedBit) {
        if (link->mNextFreeBlock == NULL) {
          // block is being returned to the heap - it is no longer allocated.
          link->mBlockSize &= ~kBlockAllocatedBit;

          vTaskSuspendAll();
          mFreeBytesRemaining += link->mBlockSize;
          insertBlockIntoFreeList (link);
          xTaskResumeAll();
          }
        }
      }
    }
  //}}}

private:
  //{{{  struct tLink_t
  typedef struct A_BLOCK_LINK {
    struct A_BLOCK_LINK* mNextFreeBlock; // The next free block in the list
    size_t mBlockSize;                   // The size of the free block
    } tLink_t;
  //}}}

  //{{{
  uint8_t* allocBlock (size_t size) {

    uint8_t* allocAddress = NULL;
    size_t largestBlock = 0;

    // The wanted size is increased so it can contain a tLink_t structure in addition to the requested amount of bytes
    size += kHeapStructSize;
    // Ensure that blocks are always aligned to the required number of bytes
    if ((size & mAlignmentMask) != 0x00)
      // Byte alignment required. */
      size += (mAlignment - (size & mAlignmentMask));

    if ((size > 0) && (size <= mFreeBytesRemaining)) {
      // Traverse the list from the start (lowest address) block until one of adequate size is found
      tLink_t* prevBlock = &mStart;
      tLink_t* block = mStart.mNextFreeBlock;
      largestBlock = mStart.mBlockSize;
      while ((block->mBlockSize < size) && (block->mNextFreeBlock != NULL)) {
        prevBlock = block;
        block = block->mNextFreeBlock;
        if (block->mBlockSize > largestBlock)
          largestBlock = block->mBlockSize;
        }

      // If the end marker was reached then a block of adequate size was not found
      if (block != mEnd) {
        // Return the memory space pointed to - jumping over the tLink_t structure at its start
        allocAddress = ((uint8_t*)prevBlock->mNextFreeBlock) + kHeapStructSize;

        //This block is being returned for use so must be taken out of the list of free blocks
        prevBlock->mNextFreeBlock = block->mNextFreeBlock;

        // If the block is larger than required it can be split into two.
        if ((block->mBlockSize - size) > kHeapMinimumBlockSize) {
          // This block is to be split into two
          // Create a new block following the number of bytes requested
          tLink_t* newLink = (tLink_t*)(((uint8_t*)block) + size);

          // Calculate the sizes of two blocks split from the single block
          newLink->mBlockSize = block->mBlockSize - size;
          block->mBlockSize = size;

          // Insert the new block into the list of free blocks
          insertBlockIntoFreeList (newLink);
          }

        mFreeBytesRemaining -= block->mBlockSize;
        if (mFreeBytesRemaining < mMinFreeBytesRemaining )
          mMinFreeBytesRemaining = mFreeBytesRemaining;

        // The block is being returned - it is allocated and owned by the application and has no "next" block. */
        block->mBlockSize |= kBlockAllocatedBit;
        block->mNextFreeBlock = NULL;
        }
      }

    return allocAddress;
    }
  //}}}
  //{{{
  void insertBlockIntoFreeList (tLink_t* insertBlock) {

    // iterate through list until block found that has higher address than block being inserted
    tLink_t* blockIt;
    for (blockIt = &mStart; blockIt->mNextFreeBlock < insertBlock; blockIt = blockIt->mNextFreeBlock) {}

    // Do the block being inserted, and the block it is being inserted after make a contiguous block of memory? */
    uint8_t* puc = (uint8_t*)blockIt;
    if ((puc + blockIt->mBlockSize) == (uint8_t*)insertBlock) {
      blockIt->mBlockSize += insertBlock->mBlockSize;
      insertBlock = blockIt;
      }

    // Do the block being inserted, and the block it is being inserted before make a contiguous block of memory?
    puc = (uint8_t*)insertBlock;
    if ((puc + insertBlock->mBlockSize) == (uint8_t*)blockIt->mNextFreeBlock) {
      if (blockIt->mNextFreeBlock != mEnd ) {
        // Form one big block from the two blocks
        insertBlock->mBlockSize += blockIt->mNextFreeBlock->mBlockSize;
        insertBlock->mNextFreeBlock = blockIt->mNextFreeBlock->mNextFreeBlock;
        }
      else
        insertBlock->mNextFreeBlock = mEnd;
      }
    else
      insertBlock->mNextFreeBlock = blockIt->mNextFreeBlock;

    // If the block being inserted plugged a gab, so was merged with the block
    // before and the block after, then it's mNextFreeBlock pointer will have
    // already been set, and should not be set here as that would make it point to itself/
    if (blockIt != insertBlock)
      blockIt->mNextFreeBlock = insertBlock;
    }
  //}}}

  const uint32_t mAlignment = 8;
  const uint32_t mAlignmentMask = 7;
  const size_t kHeapStructSize = 8;
  const size_t kHeapMinimumBlockSize = kHeapStructSize << 1;
  const size_t kBlockAllocatedBit = 0x80000000;

  tLink_t mStart;
  tLink_t* mEnd = NULL;
  size_t mSize = 0;
  size_t mFreeBytesRemaining = 0;
  size_t mMinFreeBytesRemaining = 0;
  bool mDebug = false;
  };
//}}}

// dtcm
cHeap* mDtcmHeap = nullptr;

//{{{
uint8_t* dtcmAlloc (size_t size) {
  if (!mDtcmHeap)
    mDtcmHeap = new cHeap (0x20000000, 0x00020000, false);
  return (uint8_t*)mDtcmHeap->alloc (size);
  }
//}}}
void dtcmFree (void* ptr) { mDtcmHeap->free (ptr); }
size_t getDtcmSize(){ return mDtcmHeap ? mDtcmHeap->getSize() : 0 ; }
size_t getDtcmFree() { return mDtcmHeap ? mDtcmHeap->getFree() : 0 ; }
size_t getDtcmMinFree() { return mDtcmHeap ? mDtcmHeap->getMinSize() : 0 ; }

// sram AXI
cHeap* mSramHeap = nullptr;

//{{{
void* pvPortMalloc (size_t size) {

  if (!mSramHeap)
    mSramHeap = new cHeap (0x24010000, 0x00070000, false);

  void* allocAddress = mSramHeap->alloc (size);
  if (allocAddress) {
    //printf ("pvPortMalloc %p %d\n", allocAddress, size);
    }
  else
    printf ("pvPortMalloc %d fail\n", size);

  return allocAddress;
  }
//}}}
//{{{
void vPortFree (void* ptr) {

  //printf ("vPortFree %p\n", ptr);
   mSramHeap->free (ptr);
  }
//}}}
size_t getSramSize() { return mSramHeap ? mSramHeap->getSize() : 0 ; }
size_t getSramFree() { return mSramHeap ? mSramHeap->getFree() : 0 ; }
size_t getSramMinFree() { return mSramHeap ? mSramHeap->getMinSize() : 0 ; }

//{{{
//void* operator new (size_t size) {

  //void* allocAddress = malloc (size);
  ////printf ("new %p %d\n", allocAddress, size);
  //return allocAddress;
  //}
//}}}
//{{{
//void operator delete (void* ptr) {

  ////printf ("free %p\n", ptr);
  //free (ptr);
  //}
//}}}

// sram 123
cHeap* mSram123Heap = nullptr;

//{{{
uint8_t* sram123Alloc (size_t size) {
  if (!mSram123Heap)
    mSram123Heap = new cHeap (0x30000000, 0x00048000, false);
  return (uint8_t*)mSram123Heap->alloc (size);
  }
//}}}
void sram123Free (void* ptr) { mSram123Heap->free (ptr); }
size_t getSram123Size(){ return mSram123Heap ? mSram123Heap->getSize() : 0 ; }
size_t getSram123Free() { return mSram123Heap ? mSram123Heap->getFree() : 0 ; }
size_t getSram123MinFree() { return mSram123Heap ? mSram123Heap->getMinSize() : 0 ; }

// sd ram
#define SDRAM_DEVICE_ADDR 0xD0000000
#define SDRAM_DEVICE_SIZE 0x08000000
#define LCD_WIDTH  1024
#define LCD_HEIGHT 600
heap_t* heap1 = nullptr;

//{{{
uint8_t* sdRamAlloc (size_t size) {
  if (!heap1) {
    heap1 = (heap_t*)malloc (sizeof (heap_t));
    memset (heap1, 0, sizeof (heap_t));
    init_heap (heap1, SDRAM_DEVICE_ADDR + LCD_WIDTH*LCD_HEIGHT*4, SDRAM_DEVICE_SIZE - LCD_WIDTH*LCD_HEIGHT*4);
    }

  return (uint8_t*)heap_alloc (heap1, size);
  //return mSdRamHeap->alloc (size);
  }
//}}}
void sdRamFree (void* ptr) { heap_free (heap1, ptr); }
size_t getSdRamSize() { return heap1->size; }
size_t getSdRamFree() { return heap1->free; }
size_t getSdRamMinFree() { return heap1->minFree; }
