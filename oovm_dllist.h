#ifndef __OOVM_DLLIST_H
#define __OOVM_DLLIST_H

struct ovm_dllist {
    struct ovm_dllist *prev, *next;
};

static inline struct ovm_dllist *ovm_dllist_first(const struct ovm_dllist *li)
{
    return (li->next);
}

static inline struct ovm_dllist *ovm_dllist_last(const struct ovm_dllist *li)
{
    return (li->prev);
}

static inline struct ovm_dllist *ovm_dllist_end(const struct ovm_dllist *li)
{
    return ((struct ovm_dllist *) li);
}

static inline struct ovm_dllist *ovm_dllist_prev(const struct ovm_dllist *nd)
{
    return (nd->prev);
}

static inline struct ovm_dllist *ovm_dllist_next(const struct ovm_dllist *nd)
{
    return (nd->next);
}

static inline void ovm_dllist_init(struct ovm_dllist *li)
{
    li->prev = li->next = li;
}

static inline unsigned ovm_dllist_empty(const struct ovm_dllist *li)
{
    return (li->next == li);
}

static inline void ovm_dllist_insert(struct ovm_dllist *nd, struct ovm_dllist *before)
{
    struct ovm_dllist *p = before->prev;
    nd->prev = p;
    nd->next = before;
    before->prev = p->next = nd;
}

static inline void ovm_dllist_erase(struct ovm_dllist *nd)
{
    struct ovm_dllist *p = nd->prev;
    struct ovm_dllist *q = nd->next;
    p->next = q;
    q->prev = p;
}

#endif /* __OOVM_DLLIST_H */
