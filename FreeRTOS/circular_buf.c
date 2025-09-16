#include "circular_buf.h"
#include <string.h>

#define cb_memcpy memcpy

#define CBUF_CRITICAL_INT()
    //os_mutex_create(&cbuffer->lock)

#define CBUF_ENTER_CRITICAL() \
    os_task_enter_critical(&cbuffer->lock)
    //spin_lock(&cbuffer->lock)

#define CBUF_EXIT_CRITICAL() \
    os_task_exit_critical(&cbuffer->lock)
    //spin_unlock(&cbuffer->lock)



u32 cbuf_read(cbuffer_t *cbuffer, void *buf, u32 len)
{
    u32 r_len = len;
    u32 copy_len;
    u8 *read_ptr;
    if (!cbuffer) {
        return 0;
    }
    CBUF_ENTER_CRITICAL();
    if ((u32)cbuffer->read_ptr >= (u32)cbuffer->end) {
        cbuffer->read_ptr = (u8 *)cbuffer->begin;
    }
    if (cbuffer->data_len < len) {
        /* memset(buf, 0, len); */
        CBUF_EXIT_CRITICAL();
        return 0;
    }
    read_ptr = cbuffer->read_ptr;
    copy_len = (u32)cbuffer->end - (u32)read_ptr;
    if (copy_len > len) {
        copy_len = len;
    }
    len -= copy_len;
    cb_memcpy(buf, read_ptr, copy_len);
    if (len == 0) {
        read_ptr += copy_len;
    } else {
        cb_memcpy((u8 *)buf + copy_len, cbuffer->begin, len);
        read_ptr = cbuffer->begin + len;
    }
    cbuffer->data_len -= r_len;
    cbuffer->tmp_len -= r_len;
    cbuffer->read_ptr = read_ptr;
    CBUF_EXIT_CRITICAL();
    return r_len;
}

u32 cbuf_prewrite(cbuffer_t *cbuffer, void *buf, u32 len)
{
    u32 length;
    u32 remain_len;
    u8 *tmp_ptr;
    if (!cbuffer) {
        return 0;
    }
    if ((cbuffer->total_len - cbuffer->tmp_len) < len) {
        return 0;
    }
    tmp_ptr = cbuffer->tmp_ptr;
    length = (u32)cbuffer->end - (u32)tmp_ptr;
    if (length >= len) {
        cb_memcpy(tmp_ptr, buf, len);
        tmp_ptr += len;
    } else {
        remain_len = len - length;
        cb_memcpy(tmp_ptr, buf, length);
        cb_memcpy(cbuffer->begin, ((u8 *)buf) + length, remain_len);
        tmp_ptr = (u8 *)cbuffer->begin + remain_len;
    }
    CBUF_ENTER_CRITICAL();
    cbuffer->tmp_len += len;
    cbuffer->tmp_ptr = tmp_ptr;
    CBUF_EXIT_CRITICAL();
    return len;
}

void cbuf_updata_prewrite(cbuffer_t *cbuffer)
{
    CBUF_ENTER_CRITICAL();
    cbuffer->data_len = cbuffer->tmp_len;
    cbuffer->write_ptr = cbuffer->tmp_ptr;
    CBUF_EXIT_CRITICAL();
}

void cbuf_discard_prewrite(cbuffer_t *cbuffer)
{
    CBUF_ENTER_CRITICAL();
    cbuffer->tmp_len = cbuffer->data_len ;
    cbuffer->tmp_ptr = cbuffer->write_ptr ;
    CBUF_EXIT_CRITICAL();
}

u32 cbuf_write(cbuffer_t *cbuffer, void *buf, u32 len)
{
    u32 length;
    u32 remain_len;
    if (!cbuffer) {
        return 0;
    }
    CBUF_ENTER_CRITICAL();
    if ((cbuffer->total_len - cbuffer->data_len) < len) {
        CBUF_EXIT_CRITICAL();
        return 0;
    }
    length = (u32)cbuffer->end - (u32)cbuffer->write_ptr;
    if (length >= len) {
        cb_memcpy(cbuffer->write_ptr, buf, len);
        cbuffer->write_ptr += len;
    } else {
        remain_len = len - length;
        cb_memcpy(cbuffer->write_ptr, buf, length);
        cb_memcpy(cbuffer->begin, ((u8 *)buf) + length, remain_len);
        cbuffer->write_ptr = (u8 *)cbuffer->begin + remain_len;
    }
    cbuffer->data_len += len;
    cbuffer->tmp_len = cbuffer->data_len ;
    cbuffer->tmp_ptr = cbuffer->write_ptr ;
    CBUF_EXIT_CRITICAL();
    return len;
}

u32 cbuf_is_write_able(cbuffer_t *cbuffer, u32 len)
{
    u32 w_len;
    if (!cbuffer) {
        return 0;
    }
    w_len = cbuffer->total_len - cbuffer->data_len;
    if (w_len < len) {
        return 0;
    }
    return w_len;
}

void *cbuf_write_alloc(cbuffer_t *cbuffer, u32 *len)
{
    u32 data_len;
    if (!cbuffer) {
        return 0;
    }
    CBUF_ENTER_CRITICAL();
    *len = cbuffer->end - cbuffer->write_ptr;
    data_len = cbuffer->total_len - cbuffer->data_len;
    if (*len == 0) {
        cbuffer->write_ptr = cbuffer->begin;
        *len = data_len;
    }
    if (*len > data_len) {
        *len = data_len;
    }
    CBUF_EXIT_CRITICAL();
    return cbuffer->write_ptr;
}

void cbuf_write_updata(cbuffer_t *cbuffer, u32 len)
{
    CBUF_ENTER_CRITICAL();
    cbuffer->tmp_ptr = cbuffer->write_ptr += len;
    cbuffer->tmp_len = cbuffer->data_len += len;
    CBUF_EXIT_CRITICAL();
}

void *cbuf_read_alloc(cbuffer_t *cbuffer, u32 *len)
{
    u32 data_len;
    if (!cbuffer) {
        return 0;
    }
    CBUF_ENTER_CRITICAL();
    if ((u32)cbuffer->read_ptr >= (u32)cbuffer->end) {
        cbuffer->read_ptr = (u8 *)cbuffer->begin;
    }
    data_len = cbuffer->data_len ;
    *len = (u32)cbuffer->end - (u32)cbuffer->read_ptr;
    if (data_len <= *len) {
        *len = data_len;
    }
    CBUF_EXIT_CRITICAL();
    return cbuffer->read_ptr;
}

void cbuf_read_updata(cbuffer_t *cbuffer, u32 len)
{
    CBUF_ENTER_CRITICAL();
    cbuffer->read_ptr += len;
    if ((u32)cbuffer->read_ptr >= (u32)cbuffer->end) {
        cbuffer->read_ptr = (u8 *)cbuffer->begin;
    }
    cbuffer->tmp_len -= len;
    cbuffer->data_len -= len;
    CBUF_EXIT_CRITICAL();
}

void cbuf_init(cbuffer_t *cbuffer, void *buf, u32 size)
{
    cbuffer->data_len = 0;
    cbuffer->tmp_len = 0 ;
    cbuffer->begin = buf;
    cbuffer->read_ptr = buf;
    cbuffer->write_ptr = buf;
    cbuffer->tmp_ptr = buf;
    cbuffer->end = (u8 *)buf + size;
    cbuffer->total_len = size;
    CBUF_CRITICAL_INT();
}

void cbuf_clear(cbuffer_t *cbuffer)
{
    CBUF_ENTER_CRITICAL();
    cbuffer->read_ptr = cbuffer->begin;
    cbuffer->tmp_ptr = cbuffer->write_ptr = cbuffer->begin;
    cbuffer->data_len = 0;
    cbuffer->tmp_len = 0 ;
    CBUF_EXIT_CRITICAL();
}

u32 cbuf_rewrite(cbuffer_t *cbuffer, void *begin, void *buf, u32 len)
{
    u32 length;
    u32 remain_len;
    if (!cbuffer) {
        return 0;
    }
    length = (u32)cbuffer->end - (u32)begin;
    if (length >= len) {
        cb_memcpy(cbuffer->write_ptr, buf, len);
    } else {
        remain_len = len - length;
        cb_memcpy(begin, buf, length);
        cb_memcpy(cbuffer->begin, ((u8 *)buf) + length, remain_len);
    }
    return len;
}

void *cbuf_get_writeptr(cbuffer_t *cbuffer)
{
    if ((u32)cbuffer->write_ptr >= (u32)cbuffer->end) {
        cbuffer->write_ptr = (u8 *)cbuffer->begin;
    }
    return cbuffer->write_ptr;
}

u32 cbuf_get_data_size(cbuffer_t *cbuffer)
{
    return cbuffer->data_len;
}

void *cbuf_get_readptr(cbuffer_t *cbuffer)
{
    if ((u32)cbuffer->read_ptr >= (u32)cbuffer->end) {
        cbuffer->read_ptr = (u8 *)cbuffer->begin;
    }
    return cbuffer->read_ptr;
}

u32 cbuf_read_goback(cbuffer_t *cbuffer, u32 len)
{
    if (!cbuffer) {
        return 0;
    }
    if (cbuffer->data_len + len > cbuffer->total_len) {
        return 0;
    }
    CBUF_ENTER_CRITICAL();
    cbuffer->read_ptr -= len;
    if ((u32)cbuffer->read_ptr < (u32)cbuffer->begin) {
        cbuffer->read_ptr = (u8 *)((u32)cbuffer->end - ((u32)cbuffer->begin - (u32)cbuffer->read_ptr));
    }
    cbuffer->tmp_len += len;
    cbuffer->data_len += len;
    CBUF_EXIT_CRITICAL();
    return len;
}

u32 cbuf_get_data_len(cbuffer_t *cbuffer)
{
    return cbuffer->data_len;
}

u32 cbuf_read_alloc_len(cbuffer_t *cbuffer, void *buf, u32 len)
{
    u32 r_len = len;
    u32 copy_len;
    if (!cbuffer) {
        return 0;
    }
    CBUF_ENTER_CRITICAL();
    if ((u32)cbuffer->read_ptr >= (u32)cbuffer->end) {
        cbuffer->read_ptr = (u8 *)cbuffer->begin;
    }
    if (cbuffer->data_len < len) {
        /* memset(buf, 0, len); */
        CBUF_EXIT_CRITICAL();
        return 0;
    }
    copy_len = (u32)cbuffer->end - (u32)cbuffer->read_ptr;
    if (copy_len > len) {
        copy_len = len;
    }
    len -= copy_len;
    memcpy(buf, cbuffer->read_ptr, copy_len);
    //printf_data(cbuffer->read_ptr,copy_len) ;
    if (len == 0) {
        /* cbuffer->read_ptr += copy_len; */
    } else {
        memcpy((u8 *)buf + copy_len, cbuffer->begin, len);
        //printf_data(cbuffer->begin,len);
        /* cbuffer->read_ptr = cbuffer->begin + len; */
    }
    CBUF_EXIT_CRITICAL();
    return r_len;
}

void cbuf_read_alloc_len_updata(cbuffer_t *cbuffer, u32 len)
{
    CBUF_ENTER_CRITICAL();
    cbuffer->read_ptr += len;
    if ((u32)cbuffer->read_ptr >= (u32)cbuffer->end) {
        cbuffer->read_ptr = (u8 *)cbuffer->begin + ((u32)cbuffer->read_ptr - (u32)cbuffer->end);
    }
    cbuffer->tmp_len = cbuffer->data_len -= len;
    CBUF_EXIT_CRITICAL();
}

u32 cbuf_write_front(cbuffer_t *cbuffer, void *buf, u32 len)
{
    if (!cbuffer) {
        return 0;
    }

    //如果一开始buf是空的，直接写数据
    if (cbuffer->data_len == 0) {   
        return cbuf_write(cbuffer, buf, len);
    }

    //读指针往前以len长度
    if (cbuffer->data_len + len > cbuffer->total_len) {
        return 0;
    }
    CBUF_ENTER_CRITICAL();
    cbuffer->read_ptr -= len;
    if ((u32)cbuffer->read_ptr < (u32)cbuffer->begin) {
        cbuffer->read_ptr = (u8 *)((u32)cbuffer->end - ((u32)cbuffer->begin - (u32)cbuffer->read_ptr));
    }
    cbuffer->tmp_len += len;
    cbuffer->data_len += len;


    if ((u32)cbuffer->read_ptr >= (u32)cbuffer->end) {
        cbuffer->read_ptr = (u8 *)cbuffer->begin;
    }

    //再从当前读指针写入buf的数据
    void *begin = cbuffer->read_ptr;
    u32 length;
    u32 remain_len;

    length = (u32)cbuffer->end - (u32)begin;
    if (length >= len) {
        cb_memcpy(cbuffer->write_ptr, buf, len);
    } else {
        remain_len = len - length;
        cb_memcpy(begin, buf, length);
        cb_memcpy(cbuffer->begin, ((u8 *)buf) + length, remain_len);
    }

    CBUF_EXIT_CRITICAL();
    return len;
}

