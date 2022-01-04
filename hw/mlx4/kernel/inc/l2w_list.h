#pragma once

////////////////////////////////////////////////////////
//
// TYPES
//
////////////////////////////////////////////////////////

// Use the type, defined in wdm.h
#define list_head	_LIST_ENTRY


////////////////////////////////////////////////////////
//
// MACROS
//
////////////////////////////////////////////////////////


// Define and initialize a list header
#define LIST_HEAD(name) \
	struct list_head name = { &(name), &(name) }

// Initialize a list header
#define INIT_LIST_HEAD(ptr)		InitializeListHead(ptr)

// Get to the beginning of the struct for this list entry
#define list_entry(ptr, type, member)	CONTAINING_RECORD(ptr, type, member)

// Iterate over list of 'list_els' of given 'type'
#define list_for_each_entry(list_el, head, member, type)			\
	for ( list_el = list_entry((head)->Flink, type, member);		\
		&list_el->member != (head);									\
		list_el = list_entry(list_el->member.Flink, type, member))

// Iterate backwards over list of 'list_els' of given 'type'
#define list_for_each_entry_reverse(list_el, head, member, type)	\
	for (list_el = list_entry((head)->Blink, type, member);			\
		&list_el->member != (head);									\
		list_el = list_entry(list_el->member.Blink, type, member))

// Iterate over list of given type safe against removal of list entry
#define list_for_each_entry_safe(list_el, tmp_list_el, head, member,type, tmp_type)	\
	for (list_el = list_entry((head)->Flink, type, member),							\
		tmp_list_el = list_entry(list_el->member.Flink, type, member);				\
		&list_el->member != (head);													\
		list_el = tmp_list_el,														\
		tmp_list_el = list_entry(tmp_list_el->member.Flink, tmp_type, member))


////////////////////////////////////////////////////////
//
// FUNCTIONS
//
////////////////////////////////////////////////////////

// Insert a new entry after the specified head.
static inline void list_add(struct list_head *new_entry, struct list_head *head)
{
	InsertHeadList( head, new_entry );
}

// Insert a new entry before the specified head.
static inline void list_add_tail(struct list_head *new_entry, struct list_head *head)
{
	InsertTailList( head, new_entry );
}

// Deletes entry from list.
static inline void list_del(struct list_head *entry)
{
	RemoveEntryList( entry );
}

// Tests whether a list is empty
static inline int list_empty(struct list_head *head)
{
	return IsListEmpty( head );
}

// Insert src_list into dst_list and reinitialise the emptied src_list.
static inline void list_splice_init(struct list_head *src_list,
	struct list_head *dst_list)
{
	if (!list_empty(src_list)) {
		struct list_head *first = src_list->Flink;
		struct list_head *last = src_list->Blink;
		struct list_head *at = dst_list->Flink;

		first->Blink = dst_list;
		dst_list->Flink = first;
		
		last->Flink = at;
		at->Blink = last;

		INIT_LIST_HEAD(src_list);
	}
}


// Deletes entry from list and reinitialize it
static inline void list_del_init(struct list_head *entry)
{
	RemoveEntryList( entry );
	INIT_LIST_HEAD( entry );
}

