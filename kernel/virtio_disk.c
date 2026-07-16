//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"
#include "proc.h"

#define RAID_MODE_0 0
#define RAID_MODE_1 1
#define RAID_MODE_5 5
#define NUM_DISKS 4
#define CHUNK_SIZE 1   // 1 block striping
#define SWAP_DISK_SIZE (MAX_SWAP / NUM_DISKS)
#define ROT_DELAY 7

int disk_policy = 0;
// int raid_internal = 0;

struct disk_req {
  struct buf *b;
  uint blockno;
  int write;
  //int req_id; 
  int priority;
  struct disk_req *next;
};

struct {
  struct spinlock lock;
  struct disk_req *head;
  int curr_block;
  //int policy;     // 0 FCFS, 1 SSTF
  int disk_busy;
} dqueue;

static void
enqueue(struct buf *b, int write)
{
  struct disk_req *r = kalloc();
  if(r == 0)
    panic("enqueue: no mem");

  r->b = b;
  r->blockno = b->blockno;
  r->write = write;
  //r->req_id = req_counter++;
struct proc *p = myproc();
if(p){
  r->priority = p->level;
}
else{
  r->priority = 0;
}
  r->next = 0;

  // mark as pending
  b->disk = 1;

  acquire(&dqueue.lock);

  if(dqueue.head == 0) {
    dqueue.head = r;
  } else {
    struct disk_req *t = dqueue.head;
    while(t->next)
      t = t->next;
    t->next = r;
  }

  // if disk idle → start immediately
  // if(dqueue.disk_busy == 0) {
  //   dqueue.disk_busy = 1;
  //   // we will start after releasing lock
  // }

  release(&dqueue.lock);
}

static struct disk_req*
pick_next(void)
{
  if(dqueue.head == 0)
    return 0;

  if(disk_policy == 0) {
    // FCFS
    struct disk_req *r = dqueue.head;
    dqueue.head = r->next;
    return r;
  }

  // SSTF
  int best_priority = 0x7fffffff;
  struct disk_req *p = dqueue.head;

  while(p) {
    if(p->priority < best_priority)
      best_priority = p->priority;
    p = p->next;
  }

  // Step 2: among those, apply SSTF
  struct disk_req *best = 0, *prev = 0;
  struct disk_req *pp = 0;
  p = dqueue.head;

  int best_dist = 0x7fffffff;

  while(p) {
    if(p->priority == best_priority) {
      int diff = (int)p->blockno - dqueue.curr_block;
      int dist = diff < 0 ? -diff : diff;

      if(dist < best_dist) {
        best = p;
        prev = pp;
        best_dist = dist;
      }
    }
    pp = p;
    p = p->next;
  }

  // remove selected
  if(best) {
    if(prev)
      prev->next = best->next;
    else
      dqueue.head = best->next;
  }

  return best;
}

static int raid_mode = RAID_MODE_0;
void
set_raid_mode(int mode)
{
  if(mode == 2)
    raid_mode = RAID_MODE_5;
  else
    raid_mode = mode;
}
// static int last_sector = 0;

// static int
// compute_latency(int sector)
// {
//   int seek = sector > last_sector ? sector - last_sector : last_sector - sector;
//   last_sector = sector;

//   return 10 + seek;   // simple model (valid for test)
// }

static void
raid0_map(uint blockno, int *disk_id, uint *offset)
{
  *disk_id = blockno % NUM_DISKS;
  *offset  = blockno / NUM_DISKS;
}

static void
raid1_map(uint b, int *disk_no, uint *offset)
{
  *disk_no = b % 2;     // primary disk
  *offset = b / 2;
}

static void
raid5_map(uint b, int *data_disk, int *parity_disk, uint *offset)
{
  uint stripe  = b / (NUM_DISKS - 1);
  uint pos     = b % (NUM_DISKS - 1);

  *parity_disk = stripe % NUM_DISKS;
  *offset      = stripe;
  *data_disk = (*parity_disk+1+pos)%NUM_DISKS;
  // int d = 0;
  // for(int disk_no = 0; disk_no < NUM_DISKS; disk_no++){
  //   if(disk_no == *parity_disk) continue;
  //   if(d == (int)pos){
  //     *data_disk = disk_no;
  //     break;
  //   }
  //   d++;
  // }
}

static uint
get_physical_block(int disk_no, uint offset)
{
  return SWAP_START + disk_no+ offset*NUM_DISKS;
}

// void print_queue(char *msg)
// {
//   struct disk_req *p = dqueue.head;
//   printf("%s: [ ", msg);
//   while(p){
//     printf("%d ", p->blockno);
//     p = p->next;
//   }
//   printf("]\n");
// }
struct disk_stats d_stats;

// the address of virtio mmio register r.
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

static struct disk {
  // a set (not a ring) of DMA descriptors, with which the
  // driver tells the device where to read and write individual
  // disk operations. there are NUM descriptors.
  // most commands consist of a "chain" (a linked list) of a couple of
  // these descriptors.
  struct virtq_desc *desc;

  // a ring in which the driver writes descriptor numbers
  // that the driver would like the device to process.  it only
  // includes the head descriptor of each chain. the ring has
  // NUM elements.
  struct virtq_avail *avail;

  // a ring in which the device writes descriptor numbers that
  // the device has finished processing (just the head of each chain).
  // there are NUM used ring entries.
  struct virtq_used *used;

  // our own book-keeping.
  char free[NUM];  // is a descriptor free?
  uint16 used_idx; // we've looked this far in used[2..NUM].

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  struct {
    struct buf *b;
    char status;
    int internal;
  } info[NUM];

  // disk command headers.
  // one-for-one with descriptors, for convenience.
  struct virtio_blk_req ops[NUM];
  
  struct spinlock vdisk_lock;
  
} disk;

void
virtio_disk_init(void)
{
  uint32 status = 0;

  initlock(&disk.vdisk_lock, "virtio_disk");

  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 2 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }
  
  // reset device
  *R(VIRTIO_MMIO_STATUS) = status;

  // set ACKNOWLEDGE status bit
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // set DRIVER status bit
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // negotiate features
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // re-read status to ensure FEATURES_OK is set.
  status = *R(VIRTIO_MMIO_STATUS);
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // initialize queue 0.
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  // ensure queue 0 is not in use.
  if(*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // check maximum queue size.
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  // allocate and zero queue memory.
  disk.desc = kalloc();
  disk.avail = kalloc();
  disk.used = kalloc();
  if(!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc");
  memset(disk.desc, 0, PGSIZE);
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // set queue size.
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // write physical addresses.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk.avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk.used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk.used >> 32;

  // queue is ready.
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // all NUM descriptors start out unused.
  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  d_stats.reads = 0;
  d_stats.writes = 0;
  d_stats.avg_latency = 0;
  d_stats.total_latency = 0;
  d_stats.total_requests = 0;
  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

// find a free descriptor, mark it non-free, return its index.
static int
alloc_desc()
{
  for(int i = 0; i < NUM; i++){
    if(disk.free[i]){
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
  wakeup(&disk.free[0]);
}

// free a chain of descriptors.
static void
free_chain(int i)
{
  while(1){
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int
alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

// void
// virtio_disk_rw(struct buf *b, int write)
// {
//   uint blockno = b->blockno;
//   uint mapped_block;
//   int disk = 0;

//   if(raid_mode == RAID_MODE_0){
//     raid0_map(blockno, &disk, &mapped_block);
//   }
//   else if(raid_mode == RAID_MODE_1){
//     raid1_map(blockno, &disk, &mapped_block);
//   }
//   else{
//     int parity_disk;
//     raid5_map(blockno, &disk, &parity_disk, &mapped_block);
//   }
//   uint physical_block = get_physical_block(disk, mapped_block);
//   uint sector = physical_block * (BSIZE / 512);
//   acquire(&disk.vdisk_lock);

//   // the spec's Section 5.2 says that legacy block operations use
//   // three descriptors: one for type/reserved/sector, one for the
//   // data, one for a 1-byte status result.

//   // allocate the three descriptors.
//   int idx[3];
//   while(1){
//     if(alloc3_desc(idx) == 0) {
//       break;
//     }
//     sleep(&disk.free[0], &disk.vdisk_lock);
//   }

//   // format the three descriptors.
//   // qemu's virtio-blk.c reads them.

//   struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

//   if(write)
//     buf0->type = VIRTIO_BLK_T_OUT; // write the disk
//   else
//     buf0->type = VIRTIO_BLK_T_IN; // read the disk
//   buf0->reserved = 0;
//   buf0->sector = sector;

//   disk.desc[idx[0]].addr = (uint64) buf0;
//   disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
//   disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
//   disk.desc[idx[0]].next = idx[1];

//   disk.desc[idx[1]].addr = (uint64) b->data;
//   disk.desc[idx[1]].len = BSIZE;
//   if(write)
//     disk.desc[idx[1]].flags = 0; // device reads b->data
//   else
//     disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
//   disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
//   disk.desc[idx[1]].next = idx[2];

//   disk.info[idx[0]].status = 0xff; // device writes 0 on success
//   disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
//   disk.desc[idx[2]].len = 1;
//   disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
//   disk.desc[idx[2]].next = 0;

//   // record struct buf for virtio_disk_intr().
//   b->disk = 1;
//   disk.info[idx[0]].b = b;

//   // tell the device the first index in our chain of descriptors.
//   disk.avail->ring[disk.avail->idx % NUM] = idx[0];

//   __sync_synchronize();

//   // tell the device another avail ring entry is available.
//   disk.avail->idx += 1; // not % NUM ...

//   __sync_synchronize();

//   *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

//   // Wait for virtio_disk_intr() to say request has finished.
//   while(b->disk == 1) {
//     sleep(b, &disk.vdisk_lock);
//   }

//   disk.info[idx[0]].b = 0;
//   free_chain(idx[0]);

//   release(&disk.vdisk_lock);
// }

void virtio_disk_rw_help(struct buf *b, int write);
void disk_start(void);

void
virtio_disk_rw(struct buf *b, int write)
{
  // if(raid_internal){
  //  virtio_disk_rw_help(b, write);
  //   return;
  // }
  enqueue(b, write);
  // printf("[REQ] block=%d write=%d queued\n",
  //      b->blockno,
  //      write);

  acquire(&dqueue.lock);

  if(dqueue.disk_busy == 0) {
    dqueue.disk_busy = 1;
    release(&dqueue.lock);
    disk_start();
    acquire(&dqueue.lock);
  }

  while(b->disk == 1) {
    sleep(b, &dqueue.lock);
  }

  
if(dqueue.head != 0){
    release(&dqueue.lock);
    disk_start();
}
else{
    dqueue.disk_busy = 0;
    release(&dqueue.lock);
}
}
static void
virtio_single_rw(uint sector, char *data, int write)
{

  //printf("[SINGLE_RW] sector=%d write=%d data=%p\n",sector, write, data);
  acquire(&disk.vdisk_lock);

  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0){
      //printf("[SINGLE_RW] desc %d %d %d\n",
      //      idx[0], idx[1], idx[2]);
      break;
    }
    //printf("[SINGLE_RW] waiting descriptors\n");
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];
  buf0->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64) buf0;
  disk.desc[idx[0]].len = sizeof(*buf0);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = (uint64) data;
  disk.desc[idx[1]].len = BSIZE;
  disk.desc[idx[1]].flags = write ? 0 : VRING_DESC_F_WRITE;
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff;
  disk.desc[idx[2]].addr = (uint64)&disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
  disk.desc[idx[2]].next = 0;
  //b->disk = 1;
  disk.info[idx[0]].internal = 1;
  disk.info[idx[0]].b = 0;

  disk.avail->ring[disk.avail->idx % NUM] = idx[0];
  __sync_synchronize();
  disk.avail->idx += 1;
  __sync_synchronize();
  //printf("[SINGLE_RW] notify sector=%d\n", sector);
  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
  //printf("[SINGLE_RW] waiting interrupt status=%d\n",disk.info[idx[0]].status);
  while(disk.info[idx[0]].status == 0xff){
    //printf("[SINGLE_RW] sleep status=%d\n",disk.info[idx[0]].status);
    sleep(&disk.info[idx[0]], &disk.vdisk_lock);
  }
  //disk.info[idx[0]].b = 0;
  //printf("[SINGLE_RW] done status=%d\n",disk.info[idx[0]].status);
  free_chain(idx[0]);
  release(&disk.vdisk_lock);
}
void
virtio_disk_rw_help(struct buf *b, int write)
{ 

  //printf("[VDISK_RW] blockno=%d write=%d\n",b->blockno,write);
  uint blockno = b->blockno;
  ////printf("a");
  // uint start = ticks;
  if(blockno >= SWAP_START){

    uint logical = blockno - SWAP_START;

    if(write){

      if(raid_mode == RAID_MODE_0){
        int disk_id;
        uint offset;
        raid0_map(logical, &disk_id, &offset);

        uint physical = get_physical_block(disk_id, offset);
        uint sector = physical * (BSIZE / 512);
        // printf("[RAID0] logical=%d disk=%d offset=%d physical=%d sector=%d write=%d\n",
        //     logical,
        //     disk_id,
        //     offset,
        //     physical,
        //     sector,
        //     write);
        // //printf("[RAID0] logical %d -> disk %d offset %d\n", logical, disk_id, offset);
        acquire(&disk.vdisk_lock);

        int idx[3];
        while(1){
          if(alloc3_desc(idx) == 0) break;
          sleep(&disk.free[0], &disk.vdisk_lock);
        }

        struct virtio_blk_req *buf0 = &disk.ops[idx[0]];
        buf0->type = VIRTIO_BLK_T_OUT;
        buf0->reserved = 0;
        buf0->sector = sector;

        disk.desc[idx[0]].addr = (uint64) buf0;
        disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
        disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
        disk.desc[idx[0]].next = idx[1];

        disk.desc[idx[1]].addr = (uint64) b->data;
        disk.desc[idx[1]].len = BSIZE;
        disk.desc[idx[1]].flags = 0;
        disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
        disk.desc[idx[1]].next = idx[2];

        disk.info[idx[0]].status = 0xff;
        disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
        disk.desc[idx[2]].len = 1;
        disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
        disk.desc[idx[2]].next = 0;

        b->disk = 1;
        disk.info[idx[0]].b = b;

        disk.avail->ring[disk.avail->idx % NUM] = idx[0];

        __sync_synchronize();

        disk.avail->idx += 1;

        __sync_synchronize();

        *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

        while(b->disk == 1){
          sleep(b, &disk.vdisk_lock);
        }
        d_stats.total_requests++;
        d_stats.writes++;

        disk.info[idx[0]].b = 0;
        free_chain(idx[0]);

        release(&disk.vdisk_lock);
        return;
      }

      if(raid_mode == RAID_MODE_1){
        int primary;
        uint offset;
        raid1_map(logical, &primary, &offset);

        for(int copy = 0; copy < 2; copy++){
          int disk_id = (copy == 0) ? primary : primary + 2;

          uint physical = get_physical_block(disk_id, offset);
          uint sector = physical * (BSIZE / 512);
          // printf("[RAID1] logical=%d primary=%d mirror=%d currentdisk=%d offset=%d physical=%d sector=%d write=%d\n",
          //     logical,
          //     primary,
          //     primary + 2,
          //     disk_id,
          //     offset,
          //     physical,
          //     sector,
          //     write);
      //     //printf("[RAID1] logical %d -> primary %d mirror %d offset %d\n",
      //  logical, primary, primary + 2, offset);
          acquire(&disk.vdisk_lock);

          int idx[3];
          while(1){
            if(alloc3_desc(idx) == 0) break;
            sleep(&disk.free[0], &disk.vdisk_lock);
          }
          
          struct virtio_blk_req *buf0 = &disk.ops[idx[0]];
          buf0->type = VIRTIO_BLK_T_OUT;
          buf0->reserved = 0;
          buf0->sector = sector;

          disk.desc[idx[0]].addr = (uint64) buf0;
          disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
          disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
          disk.desc[idx[0]].next = idx[1];

          disk.desc[idx[1]].addr = (uint64) b->data;
          disk.desc[idx[1]].len = BSIZE;
          disk.desc[idx[1]].flags = 0;
          disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
          disk.desc[idx[1]].next = idx[2];

          disk.info[idx[0]].status = 0xff;
          disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
          disk.desc[idx[2]].len = 1;
          disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
          disk.desc[idx[2]].next = 0;

          b->disk = 1;
          disk.info[idx[0]].b = b;

          disk.avail->ring[disk.avail->idx % NUM] = idx[0];

          __sync_synchronize();

          disk.avail->idx += 1;

          __sync_synchronize();

          *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

          while(b->disk == 1){
            sleep(b, &disk.vdisk_lock);
          }
          d_stats.total_requests++;
          d_stats.writes++;
          disk.info[idx[0]].b = 0;
          free_chain(idx[0]);

          release(&disk.vdisk_lock);
        }
        return;
      }

if(raid_mode == RAID_MODE_5){

  int data_disk = 0, parity_disk = 0;
  uint offset;

  raid5_map(logical, &data_disk, &parity_disk, &offset);

  uint data_block   = get_physical_block(data_disk, offset);
  uint parity_block = get_physical_block(parity_disk, offset);

  uint data_sector   = data_block * (BSIZE / 512);
  uint parity_sector = parity_block * (BSIZE / 512);

  // printf("[RAID5] logical=%d datadisk=%d paritydisk=%d offset=%d datablock=%d parityblock=%d datasector=%d paritysector=%d write=%d\n",
  //         logical,
  //         data_disk,
  //         parity_disk,
  //         offset,
  //         data_block,
  //         parity_block,
  //         data_sector,
  //         parity_sector,
  //         write);

  char *old_data = kalloc();
  char *old_parity = kalloc();

if(old_data == 0 || old_parity == 0){
  panic("raid5 kalloc");
}

  // READ old data + parity
  //printf("[RAID5] read old data sector=%d\n", data_sector);
  virtio_single_rw(data_sector, old_data, 0);
  //printf("[RAID5] read parity sector=%d\n", parity_sector);
  virtio_single_rw(parity_sector, old_parity, 0);

  // COMPUTE new parity
  for(int i = 0; i < BSIZE; i++){
    old_parity[i] = old_parity[i] ^ old_data[i] ^ b->data[i];
  }

  // WRITE new data + parity
  //printf("[RAID5] write data sector=%d\n", data_sector);
  virtio_single_rw(data_sector, (char*)b->data, 1);
  //printf("[RAID5] write parity sector=%d\n", parity_sector);
  virtio_single_rw(parity_sector, old_parity, 1);
  kfree(old_data);
  kfree(old_parity);  
        d_stats.total_requests+=4;
        d_stats.writes+=2;
        d_stats.reads+=2;
    acquire(&dqueue.lock);
    b->disk = 0;
    wakeup(b);
    release(&dqueue.lock);


  return;
}
    }
    // RAID READ

    uint mapped_block;
    int disk_id = 0;

    if(raid_mode == RAID_MODE_0){
      raid0_map(logical, &disk_id, &mapped_block);
    }
    else if(raid_mode == RAID_MODE_1){
      raid1_map(logical, &disk_id, &mapped_block);
    }
    else{
      int parity_disk = 0;
      raid5_map(logical, &disk_id, &parity_disk, &mapped_block);
    }

    uint physical_block = get_physical_block(disk_id, mapped_block);
    uint sector = physical_block * (BSIZE / 512);
    // printf("[RAID-READ] logical=%d disk=%d mappedblock=%d sector=%d mode=%d\n",
    //    logical,
    //    disk_id,
    //    physical_block,
    //    sector,
    //    raid_mode);

    acquire(&disk.vdisk_lock);

    int idx[3];
    while(1){
      if(alloc3_desc(idx) == 0) break;
      sleep(&disk.free[0], &disk.vdisk_lock);
    }

    struct virtio_blk_req *buf0 = &disk.ops[idx[0]];
    buf0->type = VIRTIO_BLK_T_IN;
    buf0->reserved = 0;
    buf0->sector = sector;

    disk.desc[idx[0]].addr = (uint64) buf0;
    disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
    disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
    disk.desc[idx[0]].next = idx[1];

    disk.desc[idx[1]].addr = (uint64) b->data;
    disk.desc[idx[1]].len = BSIZE;
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE;
    disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
    disk.desc[idx[1]].next = idx[2];

    disk.info[idx[0]].status = 0xff;
    disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
    disk.desc[idx[2]].next = 0;

    b->disk = 1;
    disk.info[idx[0]].b = b;
    d_stats.total_requests++;
    d_stats.reads++;

    disk.avail->ring[disk.avail->idx % NUM] = idx[0];

    __sync_synchronize();

    disk.avail->idx += 1;

    __sync_synchronize();

    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    while(b->disk == 1){
      sleep(b, &disk.vdisk_lock);
    }

    disk.info[idx[0]].b = 0;
    free_chain(idx[0]);

    release(&disk.vdisk_lock);
    return;
  }

  // NORMAL FILESYSTEM PATH

  uint sector = blockno * (BSIZE / 512);
  // printf("[FS] block=%d sector=%d write=%d\n",
  //      blockno,
  //      sector,
  //      write);

  acquire(&disk.vdisk_lock);

  // the spec's Section 5.2 says that legacy block operations use
  // three descriptors: one for type/reserved/sector, one for the
  // data, one for a 1-byte status result.

  // allocate the three descriptors.
  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0) break;
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  // format the three descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];
  buf0->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64) buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = (uint64) b->data;
  disk.desc[idx[1]].len = BSIZE;
  if(write)
    disk.desc[idx[1]].flags = 0;
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE;
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff;
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
  disk.desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  b->disk = 1;
  disk.info[idx[0]].internal = 0;
  disk.info[idx[0]].b = b;
  d_stats.total_requests++;
  if(write){
    d_stats.writes++;
  }
  else{
    d_stats.reads++;
  }

  // tell the device the first index in our chain of descriptors.
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  __sync_synchronize();

  disk.avail->idx += 1;

  __sync_synchronize();

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  while(b->disk == 1){
    sleep(b, &disk.vdisk_lock);
  }

  disk.info[idx[0]].b = 0;
  free_chain(idx[0]);

  release(&disk.vdisk_lock);
}

int myabs(int x){
  return x < 0 ? -x : x;
}

void
disk_start(void)
{
  struct disk_req *r;

  acquire(&dqueue.lock);

  if(dqueue.head == 0) {
    dqueue.disk_busy = 0;
    release(&dqueue.lock);
    return;
  }

  //  print_queue("QUEUE BEFORE");

    // printf("CURRENT HEAD: %d\n", dqueue.curr_block);

  r = pick_next();
  if(r == 0){
  dqueue.disk_busy = 0;
  release(&dqueue.lock);
  return;
}
  //PRINT SELECTED
  // printf("SELECTED: %d\n", r->blockno);
int requested = r->blockno;
// printf("DISK: curr=%d -> next=%d\n", dqueue.curr_block, r->blockno);
dqueue.curr_block = requested;
int current   = dqueue.curr_block;


// latency formula
int latency = myabs(current - requested) + ROT_DELAY;

// // update stats
d_stats.total_latency += latency;
// dstat.total_requests++;

// if(r->write){
//   dstat.writes++;
// }
// else{
//   dstat.reads++;
// }

// printf("[DISK] curr=%d req=%d lat=%d | R=%d W=%d total=%d avg=%d\n",
//        current,
//        requested,
//        latency,
//        d_stats.reads,
//        d_stats.writes,
//        d_stats.total_requests,
//        (d_stats.total_requests ? d_stats.total_latency / d_stats.total_requests : 0));





  //  print_queue("QUEUE AFTER");
  // acquire(&dqueue.lock);

//int is_test = (dqueue.head != 0);  // multiple requests exist

release(&dqueue.lock);

// if(is_test && serve_idx < MAX_LOG){
//   serve_log[serve_idx++] = r->req_id;
// }


  virtio_disk_rw_help(r->b, r->write);

  kfree(r);
}



void
virtio_disk_intr()
{
  // printf("[INTR] interrupt\n");
  acquire(&disk.vdisk_lock);

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // the device increments disk.used->idx when it
  // adds an entry to the used ring.
    int start_next = 0;
  while(disk.used_idx != disk.used->idx){
    // printf("[INTR] used_idx=%d device_idx=%d\n",disk.used_idx,disk.used->idx);
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;

    if(disk.info[id].status != 0){
      // printf("%d, %d\n",id,disk.info[id].status);
      panic("virtio_disk_intr status");
    }

    struct buf *b = disk.info[id].b;
    //b->disk = 0;   // disk is done with buf
    ////printf("id=%d b=%p status=%d\n", id, b, disk.info[id].status);
    if(b && (!disk.info[id].internal)){
        // printf("[INTR] wakeup real buf\n");
       acquire(&dqueue.lock);
        b->disk = 0;
        // printf("[DONE] block=%d\n", b->blockno);
        wakeup(b);
        release(&dqueue.lock);
    }
    else{
        // printf("[INTR] wakeup single_rw\n");
        wakeup(&disk.info[id]);
    }


    disk.used_idx += 1;
    if(!disk.info[id].internal){
    acquire(&dqueue.lock);

    if(dqueue.head != 0) {
        start_next = 1;
    } else {
        dqueue.disk_busy = 0;
    }

    release(&dqueue.lock);
}

    disk.info[id].internal = 0;
    disk.info[id].b = 0;
  }

  release(&disk.vdisk_lock);
  if(start_next){
    disk_start();
  }
}
