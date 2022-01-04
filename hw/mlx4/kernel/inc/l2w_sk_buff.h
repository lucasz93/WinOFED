#pragma once

#include "l2w_network_headers.h"

struct sk_buff 
{
    unsigned char *head;
    unsigned char *data;
    unsigned char *tail;
    unsigned char *end;
    struct ethhdr *mac;
    u32 len;
};

#define eth_hdr(skb)    (skb)->mac

static inline struct sk_buff *dev_alloc_skb(u32 length)
{
    struct sk_buff *skb = NULL;

    skb = (struct sk_buff *) kmalloc(sizeof(struct sk_buff), GFP_KERNEL);

    if(skb != NULL)
    {
        skb->head = skb->data = skb->tail = (unsigned char *) kmalloc(length, GFP_KERNEL);
        skb->end = skb->head + length;
    }
    return skb;
}

static inline void skb_reserve(struct sk_buff *skb, u32 length)
{
    skb->data += length;
    skb->tail += length;
}

static inline void kfree_skb(struct sk_buff *skb)
{
    kfree(skb->head);
    kfree(skb);
}

/*
* Function: skb_put
* Description: This function extends the used data area of the buffer. 
* If this would exceed the total buffer size the kernel will panic. A pointer to the first byte of the extra data is returned. 
*/
static inline unsigned char* skb_put(struct sk_buff *skb, u32 length)
{
    unsigned char *prev_tail = NULL;
    
    if(skb->tail + length > skb->end)
    {
        return NULL;
    }
    prev_tail = skb->tail;
    skb->tail += length;
    skb->len += length;
    return prev_tail;
}

static inline void skb_set_mac_header(struct sk_buff *skb, u32 offset)
{
	skb->mac = (struct ethhdr *) (skb->data + offset);
}

/*
* Function: skb_pull
* Description: This function removes data from the start of a buffer, returning the memory to the headroom. 
* A pointer to the next data in the buffer is returned. Once the data has been pulled future pushes will overwrite the old data
*/
static inline unsigned char * skb_pull (struct sk_buff * skb, u32 length)
{
    if(skb->data + length >= skb->tail)
    {
        return NULL;
    }
    skb->data += length;
    skb->len -= length;
    return skb->data;
}

/*
* Function: skb_push
* Description: This function extends the used data area of the buffer at the buffer start. 
* If this would exceed the total buffer headroom the kernel will panic. A pointer to the first byte of the extra data is returned. 
*/
static inline unsigned char * skb_push (struct sk_buff * skb, unsigned int length)
{
    if(skb->data - length < skb->head)
    {
        return NULL;
    }
    skb->data -= length;
    skb->len += length;
    return skb->data;
}

/*
* Function: skb_reset_mac_header
* Description: Set MAC to be in the beginning of data. 
*/
static inline void skb_reset_mac_header(struct sk_buff * skb)
{
    skb->mac = (struct ethhdr *) skb->data;
}

