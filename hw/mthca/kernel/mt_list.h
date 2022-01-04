#ifndef MT_LIST_H
#define MT_LIST_H

/* Use the type, defined in wdm.h */
#define list_head	_LIST_ENTRY

/* Define and initialize a list header */
#define LIST_HEAD(name) \
	struct list_head name = { &(name), &(name) }

/* Initialize a list header */
#define INIT_LIST_HEAD(ptr)		InitializeListHead(ptr)

/* Get to the beginning of the struct for this list entry */
#define list_entry(ptr, type, member)	CONTAINING_RECORD(ptr, type, member)

/* Iterate over list of 'list_els' of given 'type' */
#define list_for_each_entry(list_el, head, member, type)			\
	for ( list_el = list_entry((head)->Flink, type, member);		\
		&list_el->member != (head);									\
		list_el = list_entry(list_el->member.Flink, type, member))

/* Iterate backwards over list of 'list_els' of given 'type' */
#define list_for_each_entry_reverse(list_el, head, member, type)	\
	for (list_el = list_entry((head)->Blink, type, member);			\
		&list_el->member != (head);									\
		list_el = list_entry(list_el->member.Blink, type, member))

/* Iterate over list of given type safe against removal of list entry */
#define list_for_each_entry_safe(list_el, tmp_list_el, head, member,type, tmp_type)	\
	for (list_el = list_entry((head)->Flink, type, member),							\
		tmp_list_el = list_entry(list_el->member.Flink, type, member);				\
		&list_el->member != (head);													\
		list_el = tmp_list_el,														\
		tmp_list_el = list_entry(tmp_list_el->member.Flink, tmp_type, member))


/* Insert a new entry after the specified head. */
static inline void list_add(struct list_head *new_entry, struct list_head *head)
{
	InsertHeadList( head, new_entry );
}

/* Insert a new entry before the specified head. */
static inline void list_add_tail(struct list_head *new_entry, struct list_head *head)
{
	InsertTailList( head, new_entry );
}

/* Deletes entry from list. */
static inline void list_del(struct list_head *entry)
{
	RemoveEntryList( entry );
}

/* Tests whether a list is empty */
static inline int list_empty(const struct list_head *head)
{
	return IsListEmpty( head );
}


#endif
