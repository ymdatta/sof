// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2016 Intel Corporation. All rights reserved.
//
// Author: Bartosz Kokoszko <bartoszx.kokoszko@linux.intel.com>

#include <sof/audio/smart_amp.h>
#include <sof/audio/component.h>
#include <sof/trace/trace.h>
#include <sof/drivers/ipc.h>
#include <sof/ut.h>

#define trace_smart_amp(__e, ...) \
	trace_event(TRACE_CLASS_SMART_AMP, __e, ##__VA_ARGS__)

#define trace_smart_amp_with_ids(comp_ptr, format, ...)	\
	trace_event_with_ids(TRACE_CLASS_SMART_AMP,	\
			     comp_ptr->comp.pipeline_id,\
			     comp_ptr->comp.id,		\
			     format, ##__VA_ARGS__)

#define tracev_smart_amp(__e, ...) \
	tracev_event(TRACE_CLASS_SMART_AMP, __e, ##__VA_ARGS__)

#define tracev_smart_amp_with_ids(comp_ptr, format, ...)	\
	tracev_event_with_ids(TRACE_CLASS_SMART_AMP,	\
			     comp_ptr->comp.pipeline_id,\
			     comp_ptr->comp.id,		\
			     format, ##__VA_ARGS__)

#define trace_smart_amp_error(__e, ...) \
	trace_error(TRACE_CLASS_SMART_AMP, __e, ##__VA_ARGS__)

#define trace_smart_amp_error_with_ids(comp_ptr, format, ...)	\
	trace_error_with_ids(TRACE_CLASS_SMART_AMP,	\
			     comp_ptr->comp.pipeline_id,\
			     comp_ptr->comp.id,		\
			     format, ##__VA_ARGS__)

struct smart_amp_data {
	struct comp_buffer *source_buf; /**< stream source buffer */
	struct comp_buffer *feedback_buf; /**< feedback source buffer */
	struct comp_buffer *sink_buf; /**< sink buffer */
};

static struct comp_dev *smart_amp_new(struct sof_ipc_comp *comp)
{
	struct comp_dev *dev;
	struct sof_ipc_comp_process *sa;
	struct sof_ipc_comp_process *ipc_sa =
		(struct sof_ipc_comp_process *)comp;
	struct smart_amp_data *sad;

	trace_smart_amp("smart_amp_new()");

	if (IPC_IS_SIZE_INVALID(ipc_sa->config)) {
		IPC_SIZE_ERROR_TRACE(TRACE_CLASS_SMART_AMP, ipc_sa->config);
		return NULL;
	}

	dev = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
		      COMP_SIZE(struct sof_ipc_comp_process));
	if (!dev)
		return NULL;

	sa = (struct sof_ipc_comp_process *)&dev->comp;

	assert(!memcpy_s(sa, sizeof(*sa), ipc_sa,
	       sizeof(struct sof_ipc_comp_process)));

	sad = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, sizeof(*sad));

	if (!sad) {
		rfree(dev);
		return NULL;
	}

	comp_set_drvdata(dev, sad);
	dev->state = COMP_STATE_READY;

	return dev;
}

static void smart_amp_free(struct comp_dev *dev)
{
	struct smart_amp_data *sad = comp_get_drvdata(dev);

	trace_smart_amp("smart_amp_free()");

	rfree(sad);
	rfree(dev);
}

static int smart_amp_params(struct comp_dev *dev)
{
	trace_smart_amp("smart_amp_params()");

	return 0;
}

static int smart_amp_trigger(struct comp_dev *dev, int cmd)
{
	struct smart_amp_data *sad = comp_get_drvdata(dev);
	int ret = 0;

	trace_smart_amp("smart_amp_trigger(), command = %u", cmd);

	ret = comp_set_state(dev, cmd);

	if (ret == COMP_STATUS_STATE_ALREADY_SET)
		ret = PPL_STATUS_PATH_STOP;

	switch (cmd) {
	case COMP_TRIGGER_START:
	case COMP_TRIGGER_RELEASE:
		buffer_zero(sad->feedback_buf);
		break;
	case COMP_TRIGGER_PAUSE:
	case COMP_TRIGGER_STOP:
		break;
	default:
		break;
	}

	return ret;
}

static int smart_amp_demux_trigger(struct comp_dev *dev, int cmd)
{
	int ret = 0;

	trace_smart_amp("smart_amp_demux_trigger(), command = %u", cmd);

	ret = comp_set_state(dev, cmd);

	if (ret == COMP_STATUS_STATE_ALREADY_SET)
		ret = PPL_STATUS_PATH_STOP;

	return ret;
}

static int smart_amp_process_s16(struct comp_dev *dev,
				 struct comp_buffer *source,
				 struct comp_buffer *sink, uint32_t samples)
{
	int16_t *src;
	int16_t *dest;
	uint32_t buff_frag;
	int i;

	trace_smart_amp_with_ids(dev, "smart_amp_process_s16()");

	buff_frag = 0;
	for (i = 0; i < samples; i++) {
		src = buffer_read_frag_s16(source, buff_frag);
		dest = buffer_write_frag_s16(sink, buff_frag);

		*dest = *src;
		buff_frag++;
	}

	return 0;
}

static int smart_amp_process_s32(struct comp_dev *dev,
				 struct comp_buffer *source,
				 struct comp_buffer *sink, uint32_t samples)
{
	int32_t *src;
	int32_t *dest;
	uint32_t buff_frag;
	int i;

	trace_smart_amp_with_ids(dev, "smart_amp_process_s32()");

	buff_frag = 0;
	for (i = 0; i < samples; i++) {
		src = buffer_read_frag_s32(source, buff_frag);
		dest = buffer_write_frag_s32(sink, buff_frag);

		*dest = *src;
		buff_frag++;
	}

	return 0;
}

static int smart_amp_process(struct comp_dev *dev, uint32_t samples,
			     struct comp_buffer *source,
			     struct comp_buffer *sink)
{
	int ret = 0;

	switch (dev->params.frame_fmt) {
	case SOF_IPC_FRAME_S16_LE:
		ret = smart_amp_process_s16(dev, source, sink, samples);
		break;
	case SOF_IPC_FRAME_S32_LE:
		ret = smart_amp_process_s32(dev, source, sink, samples);

		break;
	default:
		trace_smart_amp_error_with_ids(dev, "smart_amp_process() "
					       "error: not supported frame "
					       "format");
		return -EINVAL;
	}

	return ret;
}

static int smart_amp_process_feedback_data(struct comp_buffer *buf,
					   uint32_t samples)
{
	(void)buf;
	(void)samples;

	/* here it is possible to process samples from feedback buf */

	return 0;
}

static int smart_amp_copy(struct comp_dev *dev)
{
	struct smart_amp_data *sad = comp_get_drvdata(dev);
	uint32_t avail_frames;
	uint32_t copy_bytes;
	uint32_t copy_samples;
	int ret = 0;

	trace_smart_amp_with_ids(dev, "smart_amp_copy()");

	/* available bytes and samples calculation */
	avail_frames = comp_avail_frames(sad->source_buf, sad->sink_buf);
	copy_bytes = avail_frames * comp_frame_bytes(dev);
	copy_samples = copy_bytes / comp_sample_bytes(dev);

	/* process data */
	smart_amp_process(dev, copy_samples, sad->source_buf, sad->sink_buf);

	/* sink and source buffer pointers update */
	comp_update_buffer_produce(sad->sink_buf, copy_bytes);
	comp_update_buffer_consume(sad->source_buf, copy_bytes);

	/* from feedback buffer we should consume as much data as we consume
	 * from source buffer.
	 */
	if (sad->feedback_buf->avail < copy_bytes) {
		trace_smart_amp_with_ids(dev, "smart_amp_copy(): not enough "
					 "data in feedback buffer");

		return ret;
	}

	trace_smart_amp_with_ids(dev, "smart_amp_copy(): processing %d "
				 "feedback bytes", copy_bytes);
	smart_amp_process_feedback_data(sad->feedback_buf, copy_samples);
	comp_update_buffer_consume(sad->feedback_buf, copy_bytes);

	return ret;
}

static int smart_amp_demux_copy(struct comp_dev *dev)
{
	struct smart_amp_data *sad = comp_get_drvdata(dev);
	uint32_t avail_frames;
	uint32_t copy_bytes;
	uint32_t copy_samples;
	int ret = 0;

	trace_smart_amp_with_ids(dev, "smart_amp_demux_copy()");

	avail_frames = comp_avail_frames(sad->source_buf, sad->sink_buf);
	copy_bytes = avail_frames * comp_frame_bytes(dev);
	copy_samples = copy_bytes / comp_sample_bytes(dev);

	trace_smart_amp_with_ids(dev, "smart_amp_demux_copy(): copy from "
				 "source_buf to sink_buf");
	smart_amp_process(dev, copy_samples, sad->source_buf, sad->sink_buf);

	trace_smart_amp_with_ids(dev, "smart_amp_demux_copy(): copy from "
				 "source_buf to feedback_buf");
	smart_amp_process(dev, copy_samples, sad->source_buf,
			  sad->feedback_buf);

	/* update buffer pointers */
	comp_update_buffer_produce(sad->sink_buf, copy_bytes);
	comp_update_buffer_produce(sad->feedback_buf, copy_bytes);
	comp_update_buffer_consume(sad->source_buf, copy_bytes);

	return ret;
}

static int smart_amp_reset(struct comp_dev *dev)
{
	trace_smart_amp("smart_amp_reset()");

	comp_set_state(dev, COMP_TRIGGER_RESET);

	return 0;
}

static int smart_amp_prepare(struct comp_dev *dev)
{
	struct sof_ipc_comp_config *config = COMP_GET_CONFIG(dev);
	struct sof_ipc_comp_process *ipc_sa =
		(struct sof_ipc_comp_process *)&dev->comp;
	struct smart_amp_data *sad = comp_get_drvdata(dev);
	struct comp_buffer *source_buffer;
	struct comp_dev *buffer_comp;
	struct list_item *blist;
	uint32_t period_bytes;
	int ret;

	(void)ipc_sa;

	trace_smart_amp("smart_amp_prepare()");

	ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
	if (ret < 0)
		return ret;

	if (ret == COMP_STATUS_STATE_ALREADY_SET)
		return PPL_STATUS_PATH_STOP;

	/* calculate period size based on config */
	period_bytes = dev->frames * comp_frame_bytes(dev);
	if (period_bytes == 0) {
		trace_smart_amp_error("smart_amp_prepare() error: "
				      "period_bytes = 0");
		return -EINVAL;
	}

	/* set downstream buffer size */
	ret = comp_set_sink_buffer(dev, period_bytes, config->periods_sink);
	if (ret < 0) {
		trace_smart_amp_error("smart_amp_prepare() error: "
				      "comp_set_sink_buffer() failed");
		return ret;
	}

	/* searching for stream and feedback source buffers */
	list_for_item(blist, &dev->bsource_list) {
		source_buffer = container_of(blist, struct comp_buffer,
					     sink_list);

		buffer_comp = buffer_get_comp(source_buffer, PPL_DIR_UPSTREAM);

		if (buffer_comp->comp.type == SOF_COMP_SMART_AMP_DEMUX)
			sad->feedback_buf = source_buffer;
		else
			sad->source_buf = source_buffer;
	}

	sad->sink_buf = list_first_item(&dev->bsink_list, struct comp_buffer,
					source_list);

	return 0;
}

static int smart_amp_demux_prepare(struct comp_dev *dev)
{
	struct sof_ipc_comp_process *ipc_sa =
		(struct sof_ipc_comp_process *)&dev->comp;
	struct smart_amp_data *sad = comp_get_drvdata(dev);
	struct comp_buffer *sink_buffer;
	struct comp_dev *buffer_comp;
	struct list_item *blist;
	uint32_t period_bytes;
	int ret;

	(void)ipc_sa;

	trace_smart_amp("smart_amp_demux_prepare()");

	ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
	if (ret < 0)
		return ret;

	if (ret == COMP_STATUS_STATE_ALREADY_SET)
		return PPL_STATUS_PATH_STOP;

	/* calculate period size based on config */
	period_bytes = dev->frames * comp_frame_bytes(dev);
	if (period_bytes == 0) {
		trace_smart_amp_error("smart_amp_prepare() error: "
				      "period_bytes = 0");
		return -EINVAL;
	}

	/* searching for stream and feedback source buffers */
	list_for_item(blist, &dev->bsink_list) {
		sink_buffer = container_of(blist, struct comp_buffer,
					   source_list);

		buffer_comp = buffer_get_comp(sink_buffer, PPL_DIR_DOWNSTREAM);

		if (buffer_comp->comp.type == SOF_COMP_SMART_AMP)
			sad->feedback_buf = sink_buffer;
		else
			sad->sink_buf = sink_buffer;
	}

	sad->source_buf = list_first_item(&dev->bsource_list,
					  struct comp_buffer, sink_list);

	return 0;
}

struct comp_driver comp_smart_amp = {
	.type = SOF_COMP_SMART_AMP,
	.ops = {
		.new = smart_amp_new,
		.free = smart_amp_free,
		.params = smart_amp_params,
		.prepare = smart_amp_prepare,
		.trigger = smart_amp_trigger,
		.copy = smart_amp_copy,
		.reset = smart_amp_reset,
	},
};

struct comp_driver comp_smart_amp_demux = {
	.type = SOF_COMP_SMART_AMP_DEMUX,
	.ops = {
		.new = smart_amp_new,
		.free = smart_amp_free,
		.params = smart_amp_params,
		.prepare = smart_amp_demux_prepare,
		.trigger = smart_amp_demux_trigger,
		.copy = smart_amp_demux_copy,
		.reset = smart_amp_reset,
	},
};

UT_STATIC void sys_comp_smart_amp_init(void)
{
	comp_register(&comp_smart_amp);
	comp_register(&comp_smart_amp_demux);
}

DECLARE_MODULE(sys_comp_smart_amp_init);
