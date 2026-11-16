/*************************************************************************
 *
 *	(c) 2004-05, 3DSP Corporation, all rights reserved.  Duplication or
 *	reproduction of any part of this software (source code, object code or
 *	comments) without the expressed written consent by 3DSP Corporation is
 *	forbidden.  For further information please contact:
 *
 *	3DSP Corporation
 *	16271 Laguna Canyon Rd
 *	Irvine, CA 92618
 *	www.3dsp.com 
 *
 *************************************************************************
 *
 * tdsp_timer.c:
 *
 * created:
 * 	??/??/????	3DSP
 *
 * description:
 * 	This source file, 
 *  
 * exports:
 *
 * make'ing:
 *
 * TODO:
 * 
 * see also:
 *
 * This source to be viewed with tabspace 4 (":se ts=4" for ex/vi)
 *
 ************************************************************************/

 


#include <linux/sched.h>	//include <linux/jiffies.h>
#include "tdsp_timer.h"
#include "tdsp_time.h"
//static char* TDSP_FILE_INDICATOR="TIMER";

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
/* Modern kernel wrapper: emulate the old (fn, data) timer API */
struct tdsp_timer_list {
	struct timer_list	tl;	/* must be first member */
	void			(*fn)(unsigned long);
	unsigned long		data;
};

static void tdsp_timer_wrapper(struct timer_list *tl)
{
	struct tdsp_timer_list *t = from_timer(t, tl, tl);
	t->fn(t->data);
}
#endif

void tdsp_timer_init(PTDSP_TIMER t, void (*timeout_handler)(unsigned long), unsigned long data)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
	struct tdsp_timer_list * tl;
	tl = (struct tdsp_timer_list *)tdsp_memory_alloc(sizeof(struct tdsp_timer_list));
	if(tl == NULL)
	{
		printk("error,tdsp_timer_init alloc memory for timer failed\n");
	}
	tl->fn = timeout_handler;
	tl->data = data;
	timer_setup(&tl->tl, tdsp_timer_wrapper, 0);
	t->timer = &tl->tl;
#else
    struct timer_list * tl;
    tl = (struct timer_list *)tdsp_memory_alloc(sizeof(struct timer_list));
    if(tl == NULL)
    {
        printk("error,tdsp_timer_init alloc memory for timer failed\n");
    }
    tl->data = (unsigned long)data;

	//assign the timeout handler, NOTE, it is a fixed handler, real timeout handler 
	//is wrapped in it
	tl->function = timeout_handler;
	//
	t->timer = tl;
    init_timer(tl);
#endif
}


void tdsp_timer_start(PTDSP_TIMER t,unsigned int timeout)
{
    struct timer_list * tl = ( struct timer_list *)t->timer;
    if(t->timer == NULL)
    {
        printk("error,tdsp_timer_start input timer is null,maybe released\n");
    }
    tl->expires = jiffies + tdsp_ms_to_ticks(timeout);
    add_timer(tl);
}




void tdsp_timer_stop(PTDSP_TIMER t)
{
	//remove timer from OS, meanwhile stop it
	struct timer_list * tl = ( struct timer_list *)t->timer;
    if(t->timer == NULL)
    {
        printk("error,tdsp_timer_stop input timer is null,maybe released\n");
    }
	del_timer(tl);
}



void tdsp_timer_reset(PTDSP_TIMER t, unsigned int sec)
{
	struct timer_list * tl = ( struct timer_list *)t->timer;
    if(t->timer == NULL)
    {
        printk("error,tdsp_timer_reset input timer is null,maybe released\n");
    }
	mod_timer(tl, jiffies + tdsp_ms_to_ticks(sec));
	return;
	
}

void tdsp_timer_kill(PTDSP_TIMER t)
{
	struct timer_list * tl = ( struct timer_list *)t->timer;
	//remove timer from OS, meanwhile stop it
	
	if (tl == NULL)
		return;
	
	del_timer(tl);
	tdsp_memory_free(tl);
	t->timer = NULL;
}

unsigned char	tdsp_get_random_byte(void)
{
	static unsigned int	retVal;

	retVal += (unsigned int)(jiffies);
	
	retVal = (29 * retVal + 73) % 1021;
	
	return (unsigned char)(retVal % 256);
}


unsigned int	tdsp_get_random_dword(void)
{
	unsigned int retVal;
	unsigned char * p_uint8=(unsigned char *)(&retVal);
	unsigned char i;

	for(i=0;i<4;i++)
	{	
		p_uint8[i] = tdsp_get_random_byte();
	}

	return retVal;
}

unsigned char  time_downout_duration(void)
{
	struct timespec64		ct_time;
	static struct timespec64	st_time;
	int				count_ms;
	unsigned char		ret = 0;

	ktime_get_coarse_real_ts64(&ct_time);

	if (st_time.tv_sec == 0)
	{
		st_time = ct_time;
		return 1;
	}

	if (ct_time.tv_sec >= st_time.tv_sec)
	{
		count_ms = (int)(ct_time.tv_sec - st_time.tv_sec) * 1000
			 + (int)(ct_time.tv_nsec - st_time.tv_nsec) / 1000;
		if (count_ms > 1000)
		{
			ret = 1;
		}
	}

	st_time = ct_time;

	return ret;
}