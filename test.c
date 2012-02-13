static struct fasync_struct *rtc_async_queue; 
static int __init rtc_init(void) 
{ 
	misc_register(&rtc_dev); 
	create_proc_read_entry("driver/rtc", 0, 0, rtc_read_proc, NULL); 

#if RTC_IRQ 
	if (rtc_has_irq == 0) 
		goto no_irq2; 

	init_timer(&rtc_irq_timer); 
	rtc_irq_timer.function = rtc_dropped_irq; 
	spin_lock_irq(&rtc_lock); 
	/* Initialize periodic freq. to CMOS reset default, which is 1024Hz */ 
	CMOS_WRITE(((CMOS_READ(RTC_FREQ_SELECT) &0xF0) | 0x06), 
		RTC_FREQ_SELECT); 
	spin_unlock_irq(&rtc_lock); 
	rtc_freq = 1024; 
no_irq2: 
#endif 

	printk(KERN_INFO "Real Time Clock Driver v" RTC_VERSION "\n"); 

	return 0; 
} 

static void __exit rtc_exit(void) 
{ 
	remove_proc_entry("driver/rtc", NULL); 
	misc_deregister(&rtc_dev); 

	release_region(RTC_PORT(0), RTC_IO_EXTENT); 
	if (rtc_has_irq) 
		free_irq(RTC_IRQ, NULL);
} 
static void rtc_interrupt(int irq, void *dev_id, struct pt_regs *regs) 
{ 
	/* 
	* Can be an alarm interrupt, update complete interrupt, 
	* or a periodic interrupt. We store the status in the 
	* low byte and the number of interrupts received since 
	* the last read in the remainder of rtc_irq_data. 
	*/ 

	spin_lock(&rtc_lock); 
	rtc_irq_data += 0x100; 
	rtc_irq_data &= ~0xff; 
	rtc_irq_data |= (CMOS_READ(RTC_INTR_FLAGS) &0xF0); 

	if (rtc_status &RTC_TIMER_ON) 
		mod_timer(&rtc_irq_timer, jiffies + HZ / rtc_freq + 2 * HZ / 100); 

	spin_unlock(&rtc_lock); 

	/* Now do the rest of the actions */ 
	wake_up_interruptible(&rtc_wait); 

	kill_fasync(&rtc_async_queue, SIGIO, POLL_IN); 
} 

static int rtc_fasync (int fd, struct file *filp, int on) 
{ 
	return fasync_helper (fd, filp, on, &rtc_async_queue); 
} 

static void rtc_dropped_irq(unsigned long data) 
{ 
	unsigned long freq; 

	spin_lock_irq(&rtc_lock); 

	/* Just in case someone disabled the timer from behind our back... */ 
	if (rtc_status &RTC_TIMER_ON) 
		mod_timer(&rtc_irq_timer, jiffies + HZ / rtc_freq + 2 * HZ / 100); 

	rtc_irq_data += ((rtc_freq / HZ) << 8); 
	rtc_irq_data &= ~0xff; 
	rtc_irq_data |= (CMOS_READ(RTC_INTR_FLAGS) &0xF0); /* restart */  
	freq = rtc_freq; 

	spin_unlock_irq(&rtc_lock); 

	printk(KERN_WARNING "rtc: lost some interrupts at %ldHz.\n", freq); 

	/* Now we have new data */ 
	wake_up_interruptible(&rtc_wait); 

	kill_fasync(&rtc_async_queue, SIGIO, POLL_IN); 
} 

