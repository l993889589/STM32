#include "string.h"
#include "stdio.h"
#include "liqueue.h"


uint32_t remain_pool(lq_handle *h);
uint32_t point_node_remain(lq_handle *h);

uint32_t lq_node_cal_size(lq_handle *h, uint32_t data_head, uint32_t data_tail);

static void *memcpy_byte(void *dst, const void *src, uint32_t size);

uint32_t remain_pool(lq_handle *h)
{
	return h->mem.pool.pool_mem_size - h->mem.pool.usage;
}

uint32_t point_node_remain(lq_handle *h)
{
	return (h->cfg.node_max - h->mem.pool.offset);
}

uint32_t lq_node_cal_size(lq_handle *h, uint32_t data_head, uint32_t data_tail)
{
	if (data_tail > data_head)
	{
		return data_tail - data_head;
	}
	else
	{
		return h->mem.pool.pool_mem_size - (data_head - data_tail);
	}
}

static void *memcpy_byte(void *dst, const void *src, uint32_t size)
{

	if (dst == 0 || src == 0 || size <= 0)
		return 0;

	register char *pdst = (char *)dst;
	register char *psrc = (char *)src;

	if (pdst > psrc && pdst < psrc + size)
	{
		pdst = pdst + size - 1;
		psrc = psrc + size - 1;

		while (size--)
			*pdst-- = *psrc--;
	}
	else
	{
		while (size--)
			*pdst++ = *psrc++;
	}
	return dst;
}

/**
 * @brief 从环形队列中取出数据
 *
 * @param src
 * @param srcsize
 * @param dest
 * @param dstsize
 * @param start
 * @param end
 * @return uint32_t
 */
uint32_t get_ring_data(unsigned char *src, uint32_t srcsize, unsigned char *dest, uint32_t dstsize, uint32_t start, uint32_t end)
{
	if (!(src && dest && srcsize && dstsize))
		return 0;

	if (start < 0 || start >= srcsize || end < 0 || end >= srcsize)
	{
		return 0;
	}

	if (start < end)
	{
		uint32_t length = end - start;

		if (length <= dstsize)
		{

			memcpy_byte(dest, src + start, length);
		}
		else
		{

			memcpy_byte(dest, src + start, dstsize);
		}

		return length;
	}
	else
	{

		uint32_t one_sec = srcsize - start;
		uint32_t two_sec = end;

		if (dstsize <= (srcsize - start))
		{
			memcpy_byte(dest, src + start, dstsize);
			return dstsize;
		}
		else
		{
			memcpy_byte(dest, src + start, one_sec); // 先拷贝第一段

			uint32_t remin = (dstsize - one_sec); // 目标容器剩下的容量

			if (remin <= (two_sec)) // 如果目标容器剩下的容量小于第二段，拷满容器。
			{
				memcpy_byte(dest + one_sec, src, remin);
				return dstsize;
			}
			else
			{
				memcpy_byte(dest + one_sec, src, two_sec);
				return one_sec + two_sec;
			}
		}
	}
}

/**
 * @brief 给环形队列添加数据
 *
 * @param dst
 * @param dst_size
 * @param src
 * @param src_size
 * @param start
 * @return int
 */
uint32_t load_ring_data(unsigned char *dst, uint32_t dst_size, unsigned char *src, uint32_t src_size, uint32_t start)
{
	if (start < 0 || start >= dst_size)
	{
		return 0;
	}
	if (!(src && src_size && dst_size))
	{
		return 0;
	}

	if (src_size <= dst_size)
	{
		uint32_t one_sec = dst_size - start;

		if (src_size <= one_sec)
		{
			memcpy_byte(dst + start, src, src_size);
			return src_size;
		}
		else
		{
			memcpy_byte(dst + start, src, one_sec);
			memcpy_byte(dst, src + one_sec, (src_size - one_sec));
			return src_size;
		}
	}
	else
	{
		uint32_t src_offset = (src_size - dst_size);
		uint32_t one_sec = dst_size - start;
		uint32_t two_sec = dst_size - one_sec;
		if (dst_size <= one_sec)
		{
			memcpy_byte(dst + start, src + src_offset, one_sec);
			return src_size;
		}
		else
		{
			memcpy_byte(dst + start, src + src_offset, one_sec);
			memcpy_byte(dst, src + one_sec + src_offset, two_sec);
			return src_size;
		}
	}
}

/**
 * @brief 节点入队
 *
 * @param list
 * @param head
 * @param tail
 * @return uint32_t
 */
uint32_t lq_node_push(lq_node_list *list, uint32_t head, uint32_t tail)
{
	if (!list->full)
	{
		list->node_list[list->node_in].head = head;
		list->node_list[list->node_in].tail = tail;
		list->node_in = (list->node_in + 1) % list->node_len;
		if (list->node_in == list->node_out)
		{
			list->full = 1;
		}
		return 1;
	}
	return 0;
}

/**
 * @brief 查询node列表是否为空
 *
 * @param list
 * @return uint32_t
 */
uint32_t lq_node_is_empty(lq_node_list *list)
{
	if ((list->node_in != list->node_out) || list->full)
	{
		// printf("node not empty\r\n");
		return 0;
	}
	return 1;
}

/**
 * @brief node清空
 *
 * @param list
 * @return uint32_t
 */
uint32_t lq_node_empty(lq_node_list *list)
{
	list->node_in = 0;
	list->node_out = 0;
	list->full = 0;
	return 0;
}

/**
 * @brief 判断node列表是否已满
 *
 * @param list
 * @param head
 * @param tail
 * @return uint32_t
 */
uint32_t lq_node_isfull(lq_node_list *list, uint32_t head, uint32_t tail)
{
	if (list->full)
	{
		return 1;
	}
	return 0;
}

/**
 * @brief node列表出队
 *
 * @param list
 * @param head
 * @param tail
 * @return uint32_t
 */
uint32_t lq_node_pop(lq_node_list *list, uint32_t *head, uint32_t *tail)
{
	if (!lq_node_is_empty(list))
	{

		*head = list->node_list[list->node_out].head;
		*tail = list->node_list[list->node_out].tail;
		list->node_out = (list->node_out + 1) % list->node_len;
		list->full = 0;
		// li_log("pop node:head[%zu],tail[%zu],new_node_out[%zu]", *head, *tail, list->node_out);
		return 1;
	}
	return 0;
}
/**
 * @brief 获取已经结算的最早的数据位置（）
 *
 * @param h
 * @return uint32_t
 */
uint32_t lq_old_tail(lq_handle *h)
{
	if (lq_node_is_empty(&h->mem.node_list))
	{
		return h->mem.node_list.node_list[h->mem.node_list.node_out].tail;
	}
	else
	{
		return h->mem.pool.tail;
	}
}

uint32_t lq_mem_init(lq_handle *h, uint32_t mem_size, uint32_t node_len)
{
	if (!(h && mem_size && node_len))
		return 0;

	memset(&h->mem, 0, sizeof(lq_mem_pool));

	h->mem.pool.pool = (uint8_t *)malloc(mem_size);
	h->mem.node_list.node_list = (lq_node *)malloc(sizeof(lq_node) * node_len);

	if (h->mem.pool.pool && h->mem.node_list.node_list)
	{
		// h->mem.node_list.node_in=0;
		// h->mem.node_list.node_out=0;
		// h->mem.node_list.full=0;

		// h->mem.pool.full=0;
		// h->mem.pool.head=0;
		// h->mem.pool.offset=0;
		// h->mem.pool.tail=0;
		// h->mem.pool.usage=0;

		h->mem.pool.pool_mem_size = mem_size;
		h->mem.node_list.node_len = node_len;
		return 1;
	}
	else
	{
		if (h->mem.pool.pool)
			free(h->mem.pool.pool);
		if (h->mem.node_list.node_list)
			free(h->mem.node_list.node_list);
		return 0;
	}
}

/**
 * @brief 以外部指针的方式装载缓存
 *
 * @param h
 * @param pool_mem
 * @param mem_size
 * @param node_mem
 * @param node_len
 * @return uint8_t
 */
uint8_t lq_mem_array_init(lq_handle *h, uint8_t *pool_mem, uint32_t mem_size, uint8_t *node_mem, uint32_t node_len)
{
	if (!(h && pool_mem && node_mem && mem_size && node_len))
		return 0;

	h->mem.pool.pool = pool_mem;
	h->mem.node_list.node_list = (lq_node *)node_mem;

	h->mem.pool.pool_mem_size = mem_size;
	h->mem.node_list.node_len = node_len;

	memset(h->mem.pool.pool, 0, mem_size);
	memset(h->mem.node_list.node_list, 0, node_len * sizeof(lq_node));
	return 0;
}

uint32_t lq_init(lq_handle *h, uint32_t node_max, OVERLAY_TYPE overlay_type)
{
	if (!(h && node_max))
		return 0;

	if (node_max > h->mem.pool.pool_mem_size)
		node_max = h->mem.pool.pool_mem_size;

	h->cfg.node_max = node_max;
	h->cfg.overlay_type = overlay_type;

	return 1;
}

/**
 * @brief 生成一个虚拟的节点，这个方法内部会操作数据指针
 *
 * @param h
 * @return uint32_t
 */
uint32_t lq_gen_node(lq_handle *h)
{
	if (lq_node_push(&h->mem.node_list, h->mem.pool.head, h->mem.pool.tail))
	{
		h->mem.pool.offset = 0;
		h->mem.pool.tail = h->mem.pool.head;
		if (h->genover)
			h->genover();
		return 1;
	}

	return 0;
}

/**
 * @brief 获取当前池子剩余容量（废弃）
 *
 * @param h
 * @return uint32_t
 */
uint32_t get_pool_remain(lq_handle *h)
{
	if (h->mem.pool.full)
		return 0;

	uint32_t old_tail = lq_old_tail(h);

	if (h->mem.pool.head > old_tail)
	{
		return h->mem.pool.pool_mem_size - (h->mem.pool.head - old_tail);
	}
	else
	{
		return old_tail - h->mem.pool.head;
	}
}

uint32_t lq_pool_load(lq_handle *h, uint8_t *data, uint32_t datalen)
{

	uint32_t load_cnt = load_ring_data(h->mem.pool.pool, h->mem.pool.pool_mem_size, data, datalen, h->mem.pool.head);
	h->mem.pool.head = (h->mem.pool.head + datalen) % h->mem.pool.pool_mem_size;

	return load_cnt;
}

/**
 * @brief 往当前的节点添加数据
 *
 * @param h
 * @param data
 * @param datalen
 * @return uint32_t
 */
uint32_t lq_add(lq_handle *h, uint8_t *data, uint32_t datalen)
{
	if (!(h && data && datalen))
	{
		return 0;
	}

	switch (h->mem.node_list.mem_mode)
	{

	case MEM_QUEUE:
	{
		
		if (h->timer.enable)
		{

			h->timer.tick = h->timer.set_timeout;
		}
		// 只有一个的装填情况
		if (datalen == 1)
		{
			switch (h->cfg.overlay_type)
			{

			case NO_OVERWRITE:
			{
				// 保护模式直接退出
				if (h->mem.node_list.full || h->mem.pool.full)
				{
					// li_log("NO_OVERWRITE mode ");
					return 0;
				}

				h->mem.pool.pool[h->mem.pool.head] = *data;
				h->mem.pool.head = (h->mem.pool.head + 1) % h->mem.pool.pool_mem_size;
				h->mem.pool.offset++;
				h->mem.pool.usage++;

				if (h->mem.pool.head == h->mem.pool.tail)
					h->mem.pool.full = 1;

				// if (h->mem.pool.usage == h->mem.pool.pool_mem_size)
				//	h->mem.pool.full = 1;

				// 生成节点
				if (h->mem.pool.offset == h->cfg.node_max)
				{
					h->mem.pool.offset = 0;
					lq_node_push(&h->mem.node_list, h->mem.pool.head, h->mem.pool.tail);

					h->mem.pool.tail = h->mem.pool.head;
				}
				return 1;
			};
			case CAN_OVERWRITE:
			{
				break;
			};
			}
		}
		else
		{
			switch (h->cfg.overlay_type)
			{

			case NO_OVERWRITE:
			{

				// 保护模式,节点不足直接退出
				if (h->mem.node_list.full || h->mem.pool.full)
					return 0;

				uint32_t remain_data = datalen;

				uint32_t p_node_remain;

				while (1)
				{

					if (remain_pool(h) && !h->mem.node_list.full)
					{

						p_node_remain = point_node_remain(h);

						// 如果当前指向的节点剩余空间大于当前要装填的，在这里就能全部装填完毕
						if (p_node_remain >= remain_data)
						{

							// 先装填
							lq_pool_load(h, data + (datalen - remain_data), remain_data);
							// 结算新的位移
							h->mem.pool.offset += remain_data;
							// 结算新的使用容量
							h->mem.pool.usage += remain_data;
							// 如果当前节点已满，开始结算一个节点
							if (h->mem.pool.offset == h->cfg.node_max)
							{
								if (h->mem.pool.usage == h->mem.pool.pool_mem_size)
									h->mem.pool.full = 1;
								// 计算节点,offset自动清零
								lq_gen_node(h);
							}
							return datalen;
						}
						else // p_node_remain < remain_data,先装载 p_node_remain 长度，（当前节点不够，先装填一部分，等下下次装填
						{

							lq_pool_load(h, data + (datalen - remain_data), p_node_remain);

							// h->mem.pool.offset += p_node_remain;
							// h->mem.pool.usage += p_node_remain;
							lq_gen_node(h);
							// 计算剩余未装载
							remain_data -= p_node_remain;

							// 结算新的使用容量
							h->mem.pool.usage += p_node_remain;
						}
						// 可以装填
					}
					else // 先装填部分
					{

						// 返回当前成功入队的长度
						return datalen - remain_data;
					}
				}

				break;
			}

			case CAN_OVERWRITE:
			{
				break;
			}
			}
		}
	}
	break;

	case MEM_BLOCK:
	{
		
		if (h->timer.enable)
		{

			h->timer.tick = h->timer.set_timeout;
		}

		if (datalen == 1)
		{
			if (h->mem.node_list.block.index < h->mem.node_list.block.buff_size)
			{

				if (h->mem.node_list.block.block_buff)
				{

					h->mem.node_list.block.block_buff[h->mem.node_list.block.index] = *data;

					h->mem.node_list.block.index++;

					if (h->mem.node_list.block.index == h->mem.node_list.block.buff_size)
					{

						h->mem.node_list.block.new_state = 1;
					}
				}
			}
		}
		else
		{

			if (h->mem.node_list.block.index + datalen < h->mem.node_list.block.buff_size)
			{
				if (h->mem.node_list.block.block_buff)
				{
					memcpy(h->mem.node_list.block.block_buff + h->mem.node_list.block.index, data, datalen);

					h->mem.node_list.block.index += datalen;

					if (h->mem.node_list.block.index == h->mem.node_list.block.buff_size - 1)
					{
						h->mem.node_list.block.new_state = 2;
						//h->mem.node_list.mem_mode = MEM_QUEUE;
					}
					if (h->mem.node_list.block.index == h->mem.node_list.block.buff_size)
					{
						h->mem.node_list.block.new_state = 3;
						//h->mem.node_list.mem_mode = MEM_QUEUE;
					}
				}
			}
			else
			{

				if (h->mem.node_list.block.block_buff)
				{
					uint32_t len = h->mem.node_list.block.buff_size - h->mem.node_list.block.index - 1;

					memcpy(h->mem.node_list.block.block_buff + h->mem.node_list.block.index, data, len);

					h->mem.node_list.block.index += len;

					h->mem.node_list.block.new_state = 4;
					h->mem.node_list.mem_mode = MEM_QUEUE;
				}
			}
		}
	}
	break;
	}

	return 0;
}

/**
 * @brief 当前有未生成点位的数据，直接生成点位
 *
 * @param h
 * @return uint32_t
 */
uint32_t lq_settle(lq_handle *h)
{

	if (!h->mem.pool.offset)
		return 0;

	return lq_gen_node(h);
}

/**
 * @brief 出队列
 *
 * @param h
 * @param buff
 * @param buffsize
 * @return uint32_t
 */
uint32_t lq_pop(lq_handle *h, uint8_t *buff, uint32_t buffsize)
{

	uint32_t data_head = 0;
	uint32_t data_tail = 0;
	// 这里的head和tail和存储的head和tail顺序是相反的。
	if (lq_node_pop(&h->mem.node_list, &data_tail, &data_head))
	{
		uint32_t node_size = lq_node_cal_size(h, data_head, data_tail);
		// log("node size:%s",node_size);
		h->mem.pool.usage -= node_size;

		uint32_t get_size;

		if (buffsize >= node_size)
		{

			get_size = get_ring_data(h->mem.pool.pool, h->mem.pool.pool_mem_size, buff, node_size, data_head, data_tail);
		}
		else
		{
			get_size = get_ring_data(h->mem.pool.pool, h->mem.pool.pool_mem_size, buff, buffsize, data_head, data_tail);
		}

		return get_size;
	}

	return 0;
}

/**
 * @brief 存入一个节点
 *
 * @param h
 * @param buff
 * @param buffsize
 * @return uint32_t
 */
uint32_t lq_push(lq_handle *h, uint8_t *buff, uint32_t buffsize)
{
	if (!(h && buff && buffsize))
		return 0;

	lq_settle(h);

	if (buffsize > h->cfg.node_max)
	{
		buffsize = h->cfg.node_max;
	}

	uint32_t ret = lq_add(h, buff, buffsize);

	if (ret)
	{
		return lq_settle(h);
	}
	return 0;
}

/**
 * @brief 自动settle模式下需要部署
 *
 * @param h
 * @param buff
 * @param buffsize
 * @return uint32_t
 */
void lq_tick(lq_handle *h, uint8_t tick)
{
	if (!h)
		return;

	if (h->timer.enable == 0)
	{

		return;
	}

	switch (h->mem.node_list.mem_mode)
	{

	case MEM_QUEUE:
	{

		if (h->timer.tick)
			h->timer.tick -= tick;

		if (h->timer.tick < tick)
		{
			// li_log("lq_gen_node");
			lq_settle(h);
		}
	};
	break;

	case MEM_BLOCK:
	{

		h->mem.node_list.block.timetick-=tick;

		if (h->mem.node_list.block.timetick <=tick)
			h->mem.node_list.block.timeout = 1;

		if (h->timer.tick)
					h->timer.tick--;

		if (h->timer.tick == 1)
		{
			h->mem.node_list.block.new_state = 5;
		}
	};
	break;
	}
}

/**
 * @brief 设置自动settle
 *
 * @param h
 * @param buff
 * @param buffsize
 * @return uint32_t
 */
uint32_t lq_timer_init(lq_handle *h, uint32_t timeout)
{
	if (!h)
		return 0;

	h->timer.tick = 0;
	h->timer.set_timeout = timeout;
	h->timer.enable = 1;

	return 1;
}

uint32_t lq_read(lq_handle *h, uint8_t *buff, uint32_t buffsize, uint32_t timeout)
{
	if (!h)
		return 0;

	// 复位
	h->mem.node_list.block.new_state = 0;
	h->mem.node_list.block.block_buff = buff;
	h->mem.node_list.block.index = 0;
	h->mem.node_list.block.buff_size = buffsize;

	h->mem.node_list.block.timeout = 0;
	h->mem.node_list.block.timetick = timeout;

	h->mem.node_list.mem_mode = MEM_BLOCK;

	for (;;)
	{
		if (h->mem.node_list.block.timeout)
		{
			h->mem.node_list.mem_mode = MEM_QUEUE;
			return 0;
		}

		if (h->mem.node_list.block.new_state)
		{
			h->mem.node_list.mem_mode = MEM_QUEUE;
			return h->mem.node_list.block.index;
		}
	}

	return 1;
}
void debug_printf(const char *format, ...);
uint32_t lq_read_ex(lq_handle *h, uint8_t *buff, uint32_t buffsize, uint32_t timeout, void (*waittask)())
{
	if (!h)
		return 0;

	// 复位
	h->mem.node_list.block.new_state = 0;
	h->mem.node_list.block.block_buff = buff;
	h->mem.node_list.block.index = 0;
	h->mem.node_list.block.buff_size = buffsize;

	h->mem.node_list.block.timeout = 0;
	h->mem.node_list.block.timetick = timeout;

	h->mem.node_list.mem_mode = MEM_BLOCK;

		int i=0;
	i--;
	
	
	for (;;)
	{

		if (waittask)
			waittask();

		if (h->mem.node_list.block.timeout)
		{
			//debug_printf("timeout");
			h->mem.node_list.mem_mode = MEM_QUEUE;
			return 0;
		}

		if (h->mem.node_list.block.new_state)
		{
			//debug_printf("timeout:%d,timetick:%d,buffsize:%d,%d",h->mem.node_list.block.timeout,h->mem.node_list.block.timetick,h->mem.node_list.block.buff_size,h->mem.node_list.mem_mode);
			h->mem.node_list.mem_mode = MEM_QUEUE;
			
			return h->mem.node_list.block.index;
		}
	}
	return 1;
}

uint32_t lq_reg_cb(lq_handle *h, void (*overtask)())
{

	if (h)
		h->genover = overtask;
}