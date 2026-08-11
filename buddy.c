#include "buddy.h"

#include <stdint.h>
#include <stdlib.h>

#define PAGE_SIZE 4096U
#define MAX_RANK 16
#define NO_PAGE (-1)

enum block_state {
    BLOCK_UNUSED = 0,
    BLOCK_FREE,
    BLOCK_ALLOCATED
};

struct page_info {
    int next;
    int prev;
    unsigned char rank;
    unsigned char state;
};

static unsigned char *pool_base;
static int pool_page_count;
static struct page_info *pages;
static int free_heads[MAX_RANK + 1];
static int free_counts[MAX_RANK + 1];

static int valid_rank(int rank)
{
    return rank >= 1 && rank <= MAX_RANK;
}

static size_t rank_pages(int rank)
{
    return (size_t)1U << (rank - 1);
}

static void add_free_block(int page, int rank)
{
    struct page_info *info = &pages[page];

    info->rank = (unsigned char)rank;
    info->state = BLOCK_FREE;
    info->prev = NO_PAGE;
    info->next = free_heads[rank];
    if (info->next != NO_PAGE)
        pages[info->next].prev = page;
    free_heads[rank] = page;
    free_counts[rank]++;
}

static void remove_free_block(int page)
{
    struct page_info *info = &pages[page];
    int rank = info->rank;

    if (info->prev == NO_PAGE)
        free_heads[rank] = info->next;
    else
        pages[info->prev].next = info->next;
    if (info->next != NO_PAGE)
        pages[info->next].prev = info->prev;

    free_counts[rank]--;
    info->next = NO_PAGE;
    info->prev = NO_PAGE;
    info->rank = 0;
    info->state = BLOCK_UNUSED;
}

static int pointer_to_page(const void *p, int *page)
{
    uintptr_t address;
    uintptr_t base;
    uintptr_t offset;
    size_t pool_size;

    if (p == NULL || pool_base == NULL || pages == NULL)
        return -EINVAL;

    address = (uintptr_t)p;
    base = (uintptr_t)pool_base;
    pool_size = (size_t)pool_page_count * PAGE_SIZE;
    if (address < base)
        return -EINVAL;

    offset = address - base;
    if (offset >= pool_size || offset % PAGE_SIZE != 0)
        return -EINVAL;

    *page = (int)(offset / PAGE_SIZE);
    return OK;
}

int init_page(void *p, int pgcount)
{
    struct page_info *new_pages;
    size_t pool_size;
    int rank;
    int page;

    if (p == NULL || pgcount <= 0)
        return -EINVAL;
    if ((size_t)pgcount > SIZE_MAX / PAGE_SIZE ||
        (uintptr_t)p > UINTPTR_MAX - (size_t)pgcount * PAGE_SIZE ||
        (size_t)pgcount > SIZE_MAX / sizeof(*new_pages))
        return -EINVAL;

    new_pages = calloc((size_t)pgcount, sizeof(*new_pages));
    if (new_pages == NULL)
        return -ENOSPC;

    free(pages);
    pages = new_pages;
    pool_base = p;
    pool_page_count = pgcount;
    for (rank = 0; rank <= MAX_RANK; rank++) {
        free_heads[rank] = NO_PAGE;
        free_counts[rank] = 0;
    }

    pool_size = (size_t)pgcount;
    page = 0;
    while ((size_t)page < pool_size) {
        size_t block_pages = 1;

        rank = 1;
        while (rank < MAX_RANK &&
               block_pages * 2 <= pool_size - (size_t)page &&
               (size_t)page % (block_pages * 2) == 0) {
            block_pages *= 2;
            rank++;
        }
        add_free_block(page, rank);
        page += (int)block_pages;
    }

    return OK;
}

void *alloc_pages(int rank)
{
    int available_rank;
    int page;

    if (!valid_rank(rank))
        return ERR_PTR(-EINVAL);
    if (pool_base == NULL || pages == NULL)
        return ERR_PTR(-ENOSPC);

    for (available_rank = rank; available_rank <= MAX_RANK;
         available_rank++) {
        if (free_heads[available_rank] != NO_PAGE)
            break;
    }
    if (available_rank > MAX_RANK)
        return ERR_PTR(-ENOSPC);

    page = free_heads[available_rank];
    remove_free_block(page);
    while (available_rank > rank) {
        int buddy;

        available_rank--;
        buddy = page + (int)rank_pages(available_rank);
        add_free_block(buddy, available_rank);
    }

    pages[page].rank = (unsigned char)rank;
    pages[page].state = BLOCK_ALLOCATED;
    return pool_base + (size_t)page * PAGE_SIZE;
}

int return_pages(void *p)
{
    int page;
    int rank;

    if (pointer_to_page(p, &page) != OK ||
        pages[page].state != BLOCK_ALLOCATED)
        return -EINVAL;

    rank = pages[page].rank;
    pages[page].rank = 0;
    pages[page].state = BLOCK_UNUSED;

    while (rank < MAX_RANK) {
        int buddy = page ^ (int)rank_pages(rank);

        if (buddy < 0 || buddy >= pool_page_count ||
            pages[buddy].state != BLOCK_FREE ||
            pages[buddy].rank != rank)
            break;

        remove_free_block(buddy);
        if (buddy < page)
            page = buddy;
        rank++;
    }

    add_free_block(page, rank);
    return OK;
}

int query_ranks(void *p)
{
    int page;
    int rank;

    if (pointer_to_page(p, &page) != OK)
        return -EINVAL;

    for (rank = 1; rank <= MAX_RANK; rank++) {
        size_t block_pages = rank_pages(rank);
        int start = page & ~((int)block_pages - 1);

        if (pages[start].state != BLOCK_UNUSED &&
            pages[start].rank == rank)
            return rank;
    }

    return -EINVAL;
}

int query_page_counts(int rank)
{
    if (!valid_rank(rank))
        return -EINVAL;
    return free_counts[rank];
}
