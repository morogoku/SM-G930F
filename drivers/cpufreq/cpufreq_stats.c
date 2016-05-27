/*
 *  drivers/cpufreq/cpufreq_stats.c
 *
 *  Copyright (C) 2003-2004 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *  (C) 2004 Zou Nan hai <nanhai.zou@intel.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/cputime.h>

static spinlock_t cpufreq_stats_lock;

struct cpufreq_stats {
	unsigned int total_trans;
	unsigned long long last_time;
	unsigned int max_state;
	unsigned int state_num;
	unsigned int last_index;
	u64 *time_in_state;
	unsigned int *freq_table;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	unsigned int *trans_table;
#endif
};

struct all_cpufreq_stats {
	unsigned int state_num;
	cputime64_t *time_in_state;
	unsigned int *freq_table;
};

struct cpufreq_power_stats {
	unsigned int state_num;
	unsigned int *curr;
	unsigned int *freq_table;
};

struct all_freq_table {
	unsigned int *freq_table;
	unsigned int table_size;
};

static struct all_freq_table *all_freq_table;

static DEFINE_PER_CPU(struct all_cpufreq_stats *, all_cpufreq_stats);
static DEFINE_PER_CPU(struct cpufreq_stats *, cpufreq_stats_table);
static DEFINE_PER_CPU(struct cpufreq_power_stats *, cpufreq_power_stats);

static int cpufreq_stats_update(struct cpufreq_stats *stats)
{
	struct all_cpufreq_stats *all_stat;
	unsigned int cpu;
	unsigned long long cur_time = get_jiffies_64();

	spin_lock(&cpufreq_stats_lock);
	all_stat = per_cpu(all_cpufreq_stats, cpu);
	if (!stats) {
		spin_unlock(&cpufreq_stats_lock);
		return 0;
	}
	stats->time_in_state[stats->last_index] += cur_time - stats->last_time;
	if (all_stat)
		all_stat->time_in_state[stats->last_index] +=
				cur_time - stats->last_time;
	stats->last_time = cur_time;
	spin_unlock(&cpufreq_stats_lock);
	return 0;
}

static ssize_t show_total_trans(struct cpufreq_policy *policy, char *buf)
{
	return sprintf(buf, "%d\n", policy->stats->total_trans);
}

static ssize_t show_time_in_state(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_stats *stats = policy->stats;
	ssize_t len = 0;
	int i;

	cpufreq_stats_update(stats);
	for (i = 0; i < stats->state_num; i++) {
		len += sprintf(buf + len, "%u %llu\n", stats->freq_table[i],
			(unsigned long long)
			jiffies_64_to_clock_t(stats->time_in_state[i]));
	}
	return len;
}

static int get_index_all_cpufreq_stat(struct all_cpufreq_stats *all_stat,
		unsigned int freq)
{
	int i;
	if (!all_stat)
		return -1;
	for (i = 0; i < all_stat->state_num; i++) {
		if (all_stat->freq_table[i] == freq)
			return i;
	}
	return -1;
}

void acct_update_power(struct task_struct *task, cputime_t cputime)
{
	struct cpufreq_power_stats *powerstats;
	struct cpufreq_stats *stats;
	unsigned int cpu_num, curr;

	if (!task)
		return;

	cpu_num = task_cpu(task);
	powerstats = per_cpu(cpufreq_power_stats, cpu_num);
	stats = per_cpu(cpufreq_stats_table, cpu_num);
	if (!powerstats || !stats)
		return;

	curr = powerstats->curr[stats->last_index];
	if (task->cpu_power != ULLONG_MAX)
		task->cpu_power += curr * cputime_to_usecs(cputime);
}
EXPORT_SYMBOL_GPL(acct_update_power);

static ssize_t show_current_in_state(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	ssize_t len = 0;
	unsigned int i, cpu;
	struct cpufreq_power_stats *powerstats;

	spin_lock(&cpufreq_stats_lock);
	for_each_possible_cpu(cpu) {
		powerstats = per_cpu(cpufreq_power_stats, cpu);
		if (!powerstats)
			continue;
		len += scnprintf(buf + len, PAGE_SIZE - len, "CPU%d:", cpu);
		for (i = 0; i < powerstats->state_num; i++)
			len += scnprintf(buf + len, PAGE_SIZE - len,
					"%d=%d ", powerstats->freq_table[i],
					powerstats->curr[i]);
		len += scnprintf(buf + len, PAGE_SIZE - len, "\n");
	}
	spin_unlock(&cpufreq_stats_lock);
	return len;
}

static ssize_t show_all_time_in_state(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	ssize_t len = 0;
	unsigned int i, cpu, freq, index;
	struct cpufreq_stats *stat;
	struct all_cpufreq_stats *all_stat;
	struct cpufreq_policy *policy;

	len += scnprintf(buf + len, PAGE_SIZE - len, "freq\t\t");
	for_each_possible_cpu(cpu) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "cpu%d\t\t", cpu);
		if (cpu_online(cpu))
			cpufreq_stats_update(stat);
	}

	if (!all_freq_table)
		goto out;
	for (i = 0; i < all_freq_table->table_size; i++) {
		freq = all_freq_table->freq_table[i];
		len += scnprintf(buf + len, PAGE_SIZE - len, "\n%u\t\t", freq);
		for_each_possible_cpu(cpu) {
			policy = cpufreq_cpu_get(cpu);
			if (policy == NULL)
				continue;
			all_stat = per_cpu(all_cpufreq_stats, policy->cpu);
			index = get_index_all_cpufreq_stat(all_stat, freq);
			if (index != -1) {
				len += scnprintf(buf + len, PAGE_SIZE - len,
					"%llu\t\t", (unsigned long long)
					cputime64_to_clock_t(all_stat->time_in_state[index]));
			} else {
				len += scnprintf(buf + len, PAGE_SIZE - len,
						"N/A\t\t");
			}
			cpufreq_cpu_put(policy);
		}
	}

out:
	len += scnprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
static ssize_t show_trans_table(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_stats *stats = policy->stats;
	ssize_t len = 0;
	int i, j;

	len += snprintf(buf + len, PAGE_SIZE - len, "   From  :    To\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "         : ");
	for (i = 0; i < stats->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
				stats->freq_table[i]);
	}
	if (len >= PAGE_SIZE)
		return PAGE_SIZE;

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	for (i = 0; i < stats->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;

		len += snprintf(buf + len, PAGE_SIZE - len, "%9u: ",
				stats->freq_table[i]);

		for (j = 0; j < stats->state_num; j++) {
			if (len >= PAGE_SIZE)
				break;
			len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
					stats->trans_table[i*stats->max_state+j]);
		}
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	}
	if (len >= PAGE_SIZE)
		return PAGE_SIZE;
	return len;
}
cpufreq_freq_attr_ro(trans_table);
#endif

cpufreq_freq_attr_ro(total_trans);
cpufreq_freq_attr_ro(time_in_state);

static struct attribute *default_attrs[] = {
	&total_trans.attr,
	&time_in_state.attr,
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	&trans_table.attr,
#endif
	NULL
};
static struct attribute_group stats_attr_group = {
	.attrs = default_attrs,
	.name = "stats"
};

static struct kobj_attribute _attr_all_time_in_state = __ATTR(all_time_in_state,
		0444, show_all_time_in_state, NULL);

static struct kobj_attribute _attr_current_in_state = __ATTR(current_in_state,
		0444, show_current_in_state, NULL);

static int freq_table_get_index(struct cpufreq_stats *stats, unsigned int freq)
{
	int index;
	for (index = 0; index < stats->max_state; index++)
		if (stats->freq_table[index] == freq)
			return index;
	return -1;
}

static void __cpufreq_stats_free_table(struct cpufreq_policy *policy)
{
	struct cpufreq_stats *stats = policy->stats;

	/* Already freed */
	if (!stats)
		return;

	pr_debug("%s: Free stats table\n", __func__);

	sysfs_remove_group(&policy->kobj, &stats_attr_group);
	kfree(stats->time_in_state);
	kfree(stats);
	policy->stats = NULL;
}

static void cpufreq_stats_free_table(unsigned int cpu)
{
	struct cpufreq_policy *policy;

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return;

	__cpufreq_stats_free_table(policy);

	cpufreq_cpu_put(policy);
}

static void cpufreq_allstats_free(void)
{
	int cpu;
	struct all_cpufreq_stats *all_stat;

	sysfs_remove_file(cpufreq_global_kobject,
						&_attr_all_time_in_state.attr);

	for_each_possible_cpu(cpu) {
		all_stat = per_cpu(all_cpufreq_stats, cpu);
		if (!all_stat)
			continue;
		kfree(all_stat->time_in_state);
		kfree(all_stat);
		per_cpu(all_cpufreq_stats, cpu) = NULL;
	}
	if (all_freq_table) {
		kfree(all_freq_table->freq_table);
		kfree(all_freq_table);
		all_freq_table = NULL;
	}
}

static void cpufreq_powerstats_free(void)
{
	int cpu;
	struct cpufreq_power_stats *powerstats;

	sysfs_remove_file(cpufreq_global_kobject, &_attr_current_in_state.attr);

	for_each_possible_cpu(cpu) {
		powerstats = per_cpu(cpufreq_power_stats, cpu);
		if (!powerstats)
			continue;
		kfree(powerstats->curr);
		kfree(powerstats);
		per_cpu(cpufreq_power_stats, cpu) = NULL;
	}
}

static int __cpufreq_stats_create_table(struct cpufreq_policy *policy,
		struct cpufreq_frequency_table *table, int count)
{
	unsigned int i = 0, ret = -ENOMEM;
	struct cpufreq_stats *stats;
	unsigned int alloc_size;
	unsigned int cpu = policy->cpu;
	struct cpufreq_frequency_table *pos;

	/* stats already initialized */
	if (policy->stats)
		return -EEXIST;

	stats = kzalloc(sizeof(*stats), GFP_KERNEL);
	if (!stats)
		return -ENOMEM;

	alloc_size = count * sizeof(int) + count * sizeof(u64);

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	alloc_size += count * count * sizeof(int);
#endif

	/* Allocate memory for time_in_state/freq_table/trans_table in one go */
	stats->time_in_state = kzalloc(alloc_size, GFP_KERNEL);
	if (!stats->time_in_state)
		goto free_stat;

	stats->freq_table = (unsigned int *)(stats->time_in_state + count);

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stats->trans_table = stats->freq_table + count;
#endif

	stats->max_state = count;

	/* Find valid-unique entries */
	cpufreq_for_each_valid_entry(pos, table)
		if (freq_table_get_index(stats, pos->frequency) == -1)
			stats->freq_table[i++] = pos->frequency;
	stats->state_num = i;

	spin_lock(&cpufreq_stats_lock);
	stats->last_time = get_jiffies_64();
	stats->last_index = freq_table_get_index(stats, policy->cur);
	spin_unlock(&cpufreq_stats_lock);

	policy->stats = stats;
	ret = sysfs_create_group(&policy->kobj, &stats_attr_group);
	if (!ret)
		return 0;

	/* We failed, release resources */
	policy->stats = NULL;
	kfree(stats->time_in_state);
free_stat:
	kfree(stats);

	return ret;
}

static void cpufreq_powerstats_create(unsigned int cpu,
		struct cpufreq_frequency_table *table, int count) {
	unsigned int alloc_size, i = 0, ret = 0;
	struct cpufreq_power_stats *powerstats;
	struct cpufreq_frequency_table *pos;
	struct device_node *cpu_node;
	char device_path[16];

	powerstats = kzalloc(sizeof(struct cpufreq_power_stats),
			GFP_KERNEL);
	if (!powerstats)
		return;

	/* Allocate memory for freq table per cpu as well as clockticks per
	 * freq*/
	alloc_size = count * sizeof(unsigned int) +
		count * sizeof(unsigned int);
	powerstats->curr = kzalloc(alloc_size, GFP_KERNEL);
	if (!powerstats->curr) {
		kfree(powerstats);
		return;
	}
	powerstats->freq_table = powerstats->curr + count;

	spin_lock(&cpufreq_stats_lock);
	i = 0;
	cpufreq_for_each_valid_entry(pos, table)
		powerstats->freq_table[i++] = pos->frequency;
	powerstats->state_num = i;

	snprintf(device_path, sizeof(device_path), "/cpus/cpu@%d", cpu);
	cpu_node = of_find_node_by_path(device_path);
	if (cpu_node) {
		ret = of_property_read_u32_array(cpu_node, "current",
				powerstats->curr, count);
		if (ret) {
			kfree(powerstats->curr);
			kfree(powerstats);
			powerstats = NULL;
		}
	}
	per_cpu(cpufreq_power_stats, cpu) = powerstats;
	spin_unlock(&cpufreq_stats_lock);
}

static int compare_for_sort(const void *lhs_ptr, const void *rhs_ptr)
{
	unsigned int lhs = *(const unsigned int *)(lhs_ptr);
	unsigned int rhs = *(const unsigned int *)(rhs_ptr);
	if (lhs < rhs)
		return -1;
	if (lhs > rhs)
		return 1;
	return 0;
}

static bool check_all_freq_table(unsigned int freq)
{
	int i;
	for (i = 0; i < all_freq_table->table_size; i++) {
		if (freq == all_freq_table->freq_table[i])
			return true;
	}
	return false;
}

static void create_all_freq_table(void)
{
	all_freq_table = kzalloc(sizeof(struct all_freq_table),
			GFP_KERNEL);
	if (!all_freq_table)
		pr_warn("could not allocate memory for all_freq_table\n");
	return;
}

static void add_all_freq_table(unsigned int freq)
{
	unsigned int size;
	size = sizeof(unsigned int) * (all_freq_table->table_size + 1);
	all_freq_table->freq_table = krealloc(all_freq_table->freq_table,
			size, GFP_ATOMIC);
	if (IS_ERR(all_freq_table->freq_table)) {
		pr_warn("Could not reallocate memory for freq_table\n");
		all_freq_table->freq_table = NULL;
		return;
	}
	all_freq_table->freq_table[all_freq_table->table_size++] = freq;
}

static void cpufreq_allstats_create(unsigned int cpu,
		struct cpufreq_frequency_table *table, int count)
{
	int i , j = 0;
	unsigned int alloc_size;
	struct all_cpufreq_stats *all_stat;
	bool sort_needed = false;

	all_stat = kzalloc(sizeof(struct all_cpufreq_stats),
			GFP_KERNEL);
	if (!all_stat) {
		pr_warn("Cannot allocate memory for cpufreq stats\n");
		return;
	}

	/*Allocate memory for freq table per cpu as well as clockticks per freq*/
	alloc_size = count * sizeof(int) + count * sizeof(cputime64_t);
	all_stat->time_in_state = kzalloc(alloc_size, GFP_KERNEL);
	if (!all_stat->time_in_state) {
		pr_warn("Cannot allocate memory for cpufreq time_in_state\n");
		kfree(all_stat);
		all_stat = NULL;
		return;
	}
	all_stat->freq_table = (unsigned int *)
		(all_stat->time_in_state + count);

	spin_lock(&cpufreq_stats_lock);
	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		all_stat->freq_table[j++] = freq;
		if (all_freq_table && !check_all_freq_table(freq)) {
			add_all_freq_table(freq);
			sort_needed = true;
		}
	}
	if (sort_needed)
		sort(all_freq_table->freq_table, all_freq_table->table_size,
				sizeof(unsigned int), &compare_for_sort, NULL);
	all_stat->state_num = j;
	per_cpu(all_cpufreq_stats, cpu) = all_stat;
	spin_unlock(&cpufreq_stats_lock);
}

static void cpufreq_stats_create_table(unsigned int cpu)
{
	struct cpufreq_policy *policy;
	struct cpufreq_frequency_table *table, *pos;
	int count = 0;
	/*
	 * "likely(!policy)" because normally cpufreq_stats will be registered
	 * before cpufreq driver
	 */
	policy = cpufreq_cpu_get(cpu);
	if (likely(!policy))
		return;

	table = cpufreq_frequency_get_table(policy->cpu);
	if (likely(table)) {
		cpufreq_for_each_valid_entry(pos, table)
			count++;

		if (!per_cpu(all_cpufreq_stats, cpu))
			cpufreq_allstats_create(cpu, table, count);
		if (!per_cpu(cpufreq_power_stats, cpu))
			cpufreq_powerstats_create(cpu, table, count);

		__cpufreq_stats_create_table(policy, table, count);
	}
	cpufreq_cpu_put(policy);
}

static int cpufreq_stat_notifier_policy(struct notifier_block *nb,
		unsigned long val, void *data)
{
	int ret = 0, count = 0;
	struct cpufreq_policy *policy = data;
	struct cpufreq_frequency_table *table, *pos;
	unsigned int cpu_num, cpu = policy->cpu;

	table = cpufreq_frequency_get_table(cpu);
	if (!table)
		return 0;

	cpufreq_for_each_valid_entry(pos, table)
		count++;

	if (!per_cpu(all_cpufreq_stats, cpu))
		cpufreq_allstats_create(cpu, table, count);

	for_each_possible_cpu(cpu_num) {
		if (!per_cpu(cpufreq_power_stats, cpu_num))
			cpufreq_powerstats_create(cpu_num, table, count);
	}

	if (val == CPUFREQ_CREATE_POLICY)
		ret = __cpufreq_stats_create_table(policy, table, count);
	else if (val == CPUFREQ_REMOVE_POLICY)
		__cpufreq_stats_free_table(policy);

	return ret;
}

static int cpufreq_stat_notifier_trans(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpufreq_policy *policy = cpufreq_cpu_get(freq->cpu);
	struct cpufreq_stats *stats;
	int old_index, new_index;

	if (!policy) {
		pr_err("%s: No policy found\n", __func__);
		return 0;
	}

	if (val != CPUFREQ_POSTCHANGE)
		goto put_policy;

	if (!policy->stats) {
		pr_debug("%s: No stats found\n", __func__);
		goto put_policy;
	}

	stats = policy->stats;

	old_index = stats->last_index;
	new_index = freq_table_get_index(stats, freq->new);

	/* We can't do stats->time_in_state[-1]= .. */
	if (old_index == -1 || new_index == -1)
		goto put_policy;

	cpufreq_stats_update(stats);

	if (old_index == new_index)
		goto put_policy;

	spin_lock(&cpufreq_stats_lock);
	stats->last_index = new_index;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stats->trans_table[old_index * stats->max_state + new_index]++;
#endif
	stats->total_trans++;
	spin_unlock(&cpufreq_stats_lock);

put_policy:
	cpufreq_cpu_put(policy);
	return 0;
}

static struct notifier_block notifier_policy_block = {
	.notifier_call = cpufreq_stat_notifier_policy
};

static struct notifier_block notifier_trans_block = {
	.notifier_call = cpufreq_stat_notifier_trans
};

static int __init cpufreq_stats_init(void)
{
	int ret;
	unsigned int cpu;

	spin_lock_init(&cpufreq_stats_lock);
	ret = cpufreq_register_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
	if (ret)
		return ret;

	for_each_online_cpu(cpu)
		cpufreq_stats_create_table(cpu);

	ret = cpufreq_register_notifier(&notifier_trans_block,
				CPUFREQ_TRANSITION_NOTIFIER);
	if (ret) {
		cpufreq_unregister_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
		for_each_online_cpu(cpu)
			cpufreq_stats_free_table(cpu);
		return ret;
	}

	create_all_freq_table();
	WARN_ON(cpufreq_get_global_kobject());
	ret = sysfs_create_file(cpufreq_global_kobject,
			&_attr_all_time_in_state.attr);
	if (ret)
		pr_warn("Cannot create sysfs file for cpufreq stats\n");

	ret = sysfs_create_file(cpufreq_global_kobject,
			&_attr_current_in_state.attr);
	if (ret)
		pr_warn("Cannot create sysfs file for cpufreq current stats\n");

	return 0;
}
static void __exit cpufreq_stats_exit(void)
{
	unsigned int cpu;

	cpufreq_unregister_notifier(&notifier_policy_block,
			CPUFREQ_POLICY_NOTIFIER);
	cpufreq_unregister_notifier(&notifier_trans_block,
			CPUFREQ_TRANSITION_NOTIFIER);
	for_each_online_cpu(cpu)
		cpufreq_stats_free_table(cpu);
	cpufreq_allstats_free();
	cpufreq_powerstats_free();
	cpufreq_put_global_kobject();
}

MODULE_AUTHOR("Zou Nan hai <nanhai.zou@intel.com>");
MODULE_DESCRIPTION("Export cpufreq stats via sysfs");
MODULE_LICENSE("GPL");

module_init(cpufreq_stats_init);
module_exit(cpufreq_stats_exit);
