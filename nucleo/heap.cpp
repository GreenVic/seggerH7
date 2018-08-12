#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "heap.h"

// 0x20000000 0x00020000  sram DTCM 128k
// ...
// 0x24000000 0x00080000  sram axi  512k
// ...
// 0x30000000 0x00020000  sram1  128k
// 0x30020000 0x00020000  sram2  128k
// 0x30040000 0x00008000  sram3  32k
// ....
// 0x30080000 0x00010000  sram4  64k

#define DTCM_ADDR 0x20000000
#define DTCM_SIZE 0x00020000
static uint8_t* mDtcmAlloc = (uint8_t*)DTCM_ADDR;
//{{{
uint8_t* dtcmAlloc (size_t bytes) {

  vTaskSuspendAll();

  uint8_t* alloc = mDtcmAlloc;
  if (alloc + bytes <= (uint8_t*)DTCM_ADDR + DTCM_SIZE)
    mDtcmAlloc += bytes;
  else
    alloc = NULL;

  xTaskResumeAll();

  return alloc;
  }
//}}}

#define SRAM123_ADDR 0x30000000
#define SRAM123_SIZE 0x00048000
static uint8_t* mSram123Alloc = (uint8_t*)SRAM123_ADDR;
//{{{
uint8_t* sram123Alloc (size_t bytes) {

  vTaskSuspendAll();

  uint8_t* alloc = mSram123Alloc;
  if (alloc + bytes <= (uint8_t*)SRAM123_ADDR + SRAM123_SIZE)
    mSram123Alloc += bytes;
  else
    alloc = NULL;

  xTaskResumeAll();

  return alloc;
  }
//}}}

//{{{
void* pvPortMalloc (size_t size) {
  vTaskSuspendAll();
  void* allocAddress = malloc (size);
  xTaskResumeAll();
  return allocAddress;
  }
//}}}
//{{{
void vPortFree (void* pv) {
  if (pv != NULL) {
    vTaskSuspendAll();
    free (pv);
    xTaskResumeAll();
    }
  }
//}}}

//{{{  defines
#define heapBITS_PER_BYTE ((size_t)8)
#define heapMINIMUM_BLOCK_SIZE ((size_t)(xHeapStructSize << 1))
//}}}
//{{{  struct tBlockLink_t
typedef struct A_BLOCK_LINK {
  struct A_BLOCK_LINK* mNextFreeBlock; // The next free block in the list
  size_t mBlockSize;                   // The size of the free block
  } tBlockLink_t;

//}}}
//{{{  statics
static const size_t xHeapStructSize =
  (sizeof(tBlockLink_t) + ((size_t)(portBYTE_ALIGNMENT-1))) & ~((size_t)portBYTE_ALIGNMENT_MASK);

static tBlockLink_t xStart;
static tBlockLink_t* pxEnd = NULL;
static size_t xFreeBytesRemaining = 0U;
static size_t xMinimumEverFreeBytesRemaining = 0U;
static size_t xBlockAllocatedBit = 0;
//}}}
//{{{
static void insertBlockIntoFreeList (tBlockLink_t* insertBlock) {

  // iterate through list until block found that has higher address than block being inserted
  tBlockLink_t* blockIt;
  for (blockIt = &xStart; blockIt->mNextFreeBlock < insertBlock; blockIt = blockIt->mNextFreeBlock) {}

  // Do the block being inserted, and the block it is being inserted after make a contiguous block of memory? */
  uint8_t* puc = (uint8_t*)blockIt;
  if ((puc + blockIt->mBlockSize) == (uint8_t*)insertBlock) {
    blockIt->mBlockSize += insertBlock->mBlockSize;
    insertBlock = blockIt;
    }

  // Do the block being inserted, and the block it is being inserted before make a contiguous block of memory?
  puc = (uint8_t*)insertBlock;
  if ((puc + insertBlock->mBlockSize) == (uint8_t*)blockIt->mNextFreeBlock) {
    if (blockIt->mNextFreeBlock != pxEnd ) {
      // Form one big block from the two blocks
      insertBlock->mBlockSize += blockIt->mNextFreeBlock->mBlockSize;
      insertBlock->mNextFreeBlock = blockIt->mNextFreeBlock->mNextFreeBlock;
      }
    else
      insertBlock->mNextFreeBlock = pxEnd;
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

//{{{
void sdRamInit (uint32_t start, uint32_t size) {

  tBlockLink_t* pxFirstFreeBlock;
  uint8_t* pucAlignedHeap;

  // Ensure the heap starts on a correctly aligned boundary
  size_t uxAddress = (size_t)start;
  size_t xTotalHeapSize = size;
  if ((uxAddress & portBYTE_ALIGNMENT_MASK) != 0) {
    uxAddress += (portBYTE_ALIGNMENT - 1);
    uxAddress &= ~((size_t)portBYTE_ALIGNMENT_MASK);
    xTotalHeapSize -= uxAddress - (size_t)start;
    }

  pucAlignedHeap = (uint8_t*)uxAddress;

  // xStart is used to hold a pointer to the first item in the list of free blocks.
  xStart.mNextFreeBlock = (tBlockLink_t*)pucAlignedHeap;
  xStart.mBlockSize = (size_t)0;

  // pxEnd is used to mark the end of the list of free blocks and is inserted at the end of the heap space. */
  uxAddress = ((size_t)pucAlignedHeap) + xTotalHeapSize;
  uxAddress -= xHeapStructSize;
  uxAddress &= ~((size_t) portBYTE_ALIGNMENT_MASK );
  pxEnd = (tBlockLink_t*)uxAddress;
  pxEnd->mBlockSize = 0;
  pxEnd->mNextFreeBlock = NULL;

  // start with single free block sized to take up entire heap space, minus space taken by pxEnd
  pxFirstFreeBlock = (tBlockLink_t*)pucAlignedHeap;
  pxFirstFreeBlock->mBlockSize = uxAddress - (size_t)pxFirstFreeBlock;
  pxFirstFreeBlock->mNextFreeBlock = pxEnd;

  // Only one block exists - and it covers the entire usable heap space
  xMinimumEverFreeBytesRemaining = pxFirstFreeBlock->mBlockSize;
  xFreeBytesRemaining = pxFirstFreeBlock->mBlockSize;

  // Work out the position of the top bit in a size_t variable
  xBlockAllocatedBit = ((size_t)1) << ((sizeof(size_t) * heapBITS_PER_BYTE) - 1);
  }
//}}}
//{{{
void* sdRamAlloc (size_t size) {

  tBlockLink_t* block;
  tBlockLink_t* previousBlock;
  tBlockLink_t* newBlockLink;
  void* allocAddress = NULL;

  vTaskSuspendAll();
    {
    // Check the requested block size is not so large that the top bit is set.
    // The top bit of the block size member of the tBlockLink_t structure is used to determine who owns the block
    // - the application or the kernel, so it must be free.
    if ((size & xBlockAllocatedBit) == 0) {
      // The wanted size is increased so it can contain a tBlockLink_t structure in addition to the requested amount of bytes. */
      if (size > 0) {
        size += xHeapStructSize;

        // Ensure that blocks are always aligned to the required number of bytes. */
        if ((size & portBYTE_ALIGNMENT_MASK ) != 0x00)
          // Byte alignment required. */
          size += (portBYTE_ALIGNMENT - (size & portBYTE_ALIGNMENT_MASK));
        }

      if ((size > 0) && (size <= xFreeBytesRemaining)) {
        // Traverse the list from the start (lowest address) block until one of adequate size is found. */
        previousBlock = &xStart;
        block = xStart.mNextFreeBlock;
        while ((block->mBlockSize < size) && (block->mNextFreeBlock != NULL)) {
          previousBlock = block;
          block = block->mNextFreeBlock;
          }

        // If the end marker was reached then a block of adequate size was not found
        if (block != pxEnd) {
          // Return the memory space pointed to - jumping over the tBlockLink_t structure at its start
          allocAddress = (void*)(((uint8_t*)previousBlock->mNextFreeBlock) + xHeapStructSize);

          //This block is being returned for use so must be taken out of the list of free blocks
          previousBlock->mNextFreeBlock = block->mNextFreeBlock;

          // If the block is larger than required it can be split into two.
          if ((block->mBlockSize - size) > heapMINIMUM_BLOCK_SIZE) {
            // This block is to be split into two
            // Create a new block following the number of bytes requested
            // The void cast is used to prevent byte alignment warnings from the compiler
            newBlockLink = (tBlockLink_t*)(((uint8_t*)block) + size);

            // Calculate the sizes of two blocks split from the single block
            newBlockLink->mBlockSize = block->mBlockSize - size;
            block->mBlockSize = size;

            // Insert the new block into the list of free blocks
            insertBlockIntoFreeList (newBlockLink);
            }

          xFreeBytesRemaining -= block->mBlockSize;
          if (xFreeBytesRemaining < xMinimumEverFreeBytesRemaining )
            xMinimumEverFreeBytesRemaining = xFreeBytesRemaining;

          // The block is being returned - it is allocated and owned by the application and has no "next" block. */
          block->mBlockSize |= xBlockAllocatedBit;
          block->mNextFreeBlock = NULL;
          }
        }
      }
    }
  xTaskResumeAll();

  printf ("sdramAlloc %p %d\n", allocAddress, size);
  return allocAddress;
  }
//}}}
//{{{
void sdRamFree (void* p) {

  printf ("sdRamFree %p\n", p);

  uint8_t* puc = (uint8_t*)p;
  tBlockLink_t* pxLink;

  if (p != NULL) {
    // The memory being freed will have an tBlockLink_t structure immediately before it.
    puc -= xHeapStructSize;

    // casting is to keep the compiler from issuing warnings.
    pxLink = (tBlockLink_t*)puc;

    if ((pxLink->mBlockSize & xBlockAllocatedBit ) != 0) {
      if (pxLink->mNextFreeBlock == NULL) {
        // The block is being returned to the heap - it is no longer allocated.
        pxLink->mBlockSize &= ~xBlockAllocatedBit;

        vTaskSuspendAll();
          {
          // Add this block to the list of free blocks.
          xFreeBytesRemaining += pxLink->mBlockSize;
          insertBlockIntoFreeList (pxLink);
          }
        xTaskResumeAll();
        }
      }
    }
  }
//}}}
size_t getSdRamFreeHeapSize() { return xFreeBytesRemaining; }
size_t getSdRamGetMinEverHeapSize() { return xMinimumEverFreeBytesRemaining; }
