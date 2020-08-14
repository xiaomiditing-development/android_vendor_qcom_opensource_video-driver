// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include "hfi_packet.h"
#include "msm_vidc_core.h"
#include "msm_vidc_inst.h"
#include "msm_vidc_driver.h"
#include "msm_vidc_debug.h"
#include "msm_vidc_platform.h"

u32 get_hfi_port(struct msm_vidc_inst *inst,
	enum msm_vidc_buffer_type buffer_type)
{
	u32 hfi_port = HFI_PORT_NONE;

	if (is_decode_session(inst)) {
		switch(buffer_type) {
		case MSM_VIDC_BUF_INPUT:
		case MSM_VIDC_BUF_INPUT_META:
			hfi_port = HFI_PORT_BITSTREAM;
			break;
		case MSM_VIDC_BUF_OUTPUT:
		case MSM_VIDC_BUF_OUTPUT_META:
			hfi_port = HFI_PORT_RAW;
			break;
		default:
			s_vpr_e(inst->sid, "%s: invalid buffer type %d\n",
				__func__, buffer_type);
			break;
		}
	} else if (is_encode_session(inst)) {
		switch (buffer_type) {
		case MSM_VIDC_BUF_INPUT:
		case MSM_VIDC_BUF_INPUT_META:
			hfi_port = HFI_PORT_RAW;
			break;
		case MSM_VIDC_BUF_OUTPUT:
		case MSM_VIDC_BUF_OUTPUT_META:
			hfi_port = HFI_PORT_BITSTREAM;
			break;
		default:
			s_vpr_e(inst->sid, "%s: invalid buffer type %d\n",
				__func__, buffer_type);
			break;
		}
	} else {
		s_vpr_e(inst->sid, "%s: invalid domain %#x\n",
			__func__, inst->domain);
	}

	return hfi_port;
}

u32 get_hfi_buffer_type(enum msm_vidc_domain_type domain,
	enum msm_vidc_buffer_type buffer_type)
{
	switch (buffer_type) {
	case MSM_VIDC_BUF_INPUT:
		if (domain == MSM_VIDC_DECODER)
			return HFI_BUFFER_BITSTREAM;
		else
			return HFI_BUFFER_RAW;
	case MSM_VIDC_BUF_OUTPUT:
		if (domain == MSM_VIDC_DECODER)
			return HFI_BUFFER_RAW;
		else
			return HFI_BUFFER_BITSTREAM;
	case MSM_VIDC_BUF_INPUT_META:
	case MSM_VIDC_BUF_OUTPUT_META:
		return HFI_BUFFER_METADATA;
	case MSM_VIDC_BUF_SCRATCH:
		return HFI_BUFFER_SCRATCH;
	case MSM_VIDC_BUF_SCRATCH_1:
		return HFI_BUFFER_SCRATCH_1;
	case MSM_VIDC_BUF_SCRATCH_2:
		return HFI_BUFFER_SCRATCH_2;
	case MSM_VIDC_BUF_PERSIST:
		return HFI_BUFFER_PERSIST;
	case MSM_VIDC_BUF_PERSIST_1:
		return HFI_BUFFER_PERSIST_1;
	default:
		d_vpr_e("invalid buffer type %d\n",
			buffer_type);
		return 0;
	}
}

u32 get_hfi_codec(struct msm_vidc_inst *inst)
{
	switch (inst->codec) {
	case MSM_VIDC_H264:
		if (inst->domain == MSM_VIDC_ENCODER)
			return HFI_CODEC_ENCODE_AVC;
		else
			return HFI_CODEC_DECODE_AVC;
	case MSM_VIDC_HEVC:
		if (inst->domain == MSM_VIDC_ENCODER)
			return HFI_CODEC_ENCODE_HEVC;
		else
			return HFI_CODEC_DECODE_HEVC;
	case MSM_VIDC_VP9:
		return HFI_CODEC_DECODE_VP9;
	case MSM_VIDC_MPEG2:
		return HFI_CODEC_DECODE_MPEG2;
	default:
		d_vpr_e("invalid codec %d, domain %d\n",
			inst->codec, inst->domain);
		return 0;
	}
}

int get_hfi_buffer(struct msm_vidc_inst *inst,
	struct msm_vidc_buffer *buffer, struct hfi_buffer *buf)
{
	if (!inst || !buffer || !buf) {
		d_vpr_e("%: invalid params\n", __func__);
		return -EINVAL;
	}

	memset(buf, 0, sizeof(struct hfi_buffer));
	buf->type = get_hfi_buffer_type(inst->domain, buffer->type);
	buf->index = buffer->index;
	buf->base_address = buffer->device_addr;
	buf->addr_offset = 0;
	buf->buffer_size = buffer->buffer_size;
	buf->data_offset = buffer->data_offset;
	buf->data_size = buffer->data_size;
	if (buffer->attr & MSM_VIDC_ATTR_READ_ONLY)
		buf->flags |= HFI_BUF_HOST_FLAG_READONLY;
	if (buffer->attr & MSM_VIDC_ATTR_PENDING_RELEASE)
		buf->flags |= HFI_BUF_HOST_FLAG_RELEASE;
	buf->timestamp = buffer->timestamp;

	return 0;
}

int hfi_create_header(u8 *pkt, u32 session_id,
	u32 header_id, u32 num_packets, u32 total_size)
{
	struct hfi_header *hdr = (struct hfi_header *)pkt;

	memset(hdr, 0, sizeof(struct hfi_header));

	hdr->size = total_size;
	hdr->session_id = session_id;
	hdr->header_id = header_id;
	hdr->num_packets = num_packets;
	return 0;
}

int hfi_create_packet(u8 *packet, u32 packet_size, u32 *offset,
	u32 pkt_type, u32 pkt_flags, u32 payload_type, u32 port,
	u32 packet_id, void *payload, u32 payload_size)
{
	u32 available_size = packet_size - *offset;
	u32 pkt_size = sizeof(struct hfi_packet) + payload_size;
	struct hfi_packet *pkt = (struct hfi_packet *)(packet + *offset);

	if (available_size < pkt_size) {
		d_vpr_e("%s: Bad packet Size for packet type %d\n",
			__func__, pkt_type);
		return -EINVAL;
	}

	memset(pkt, 0, pkt_size);

	pkt->size = pkt_size;
	pkt->type = pkt_type;
	pkt->flags = pkt_flags;
	pkt->payload_info = payload_type;
	pkt->port = port;
	pkt->packet_id = packet_id;
	if (payload_size)
		memcpy((u8 *)pkt + sizeof(struct hfi_packet),
			payload, payload_size);
	*offset = *offset + pkt->size;
	return 0;
}

int hfi_packet_sys_init(struct msm_vidc_core *core,
	u8 *pkt, u32 pkt_size)
{
	int rc = 0;
	u32 offset = 0, payload = 0, num_packets = 0;

	if (!core || !pkt) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	if (pkt_size < sizeof(struct hfi_header)) {
		d_vpr_e("%s: Invalid packet size\n", __func__);
		return -EINVAL;
	}

	/* HFI_CMD_SYSTEM_INIT */
	offset = sizeof(struct hfi_header);
	payload = HFI_VIDEO_ARCH_OX;
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_CMD_INIT,
				   (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
				   HFI_HOST_FLAGS_INTR_REQUIRED |
				   HFI_HOST_FLAGS_NON_DISCARDABLE),
				   HFI_PAYLOAD_U32,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_sys_init;
	num_packets++;

	/* HFI_PROP_INTRA_FRAME_POWER_COLLAPSE */
	payload = 0;
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_INTRA_FRAME_POWER_COLLAPSE,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_U32,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_sys_init;
	num_packets++;

	/* HFI_PROP_UBWC_MAX_CHANNELS */
	payload = core->platform->data.ubwc_config->max_channels;
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_UBWC_MAX_CHANNELS,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_U32,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_sys_init;
	num_packets++;

	/* HFI_PROP_UBWC_MAL_LENGTH */
	payload = core->platform->data.ubwc_config->mal_length;
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_UBWC_MAL_LENGTH,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_U32,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_sys_init;
	num_packets++;

	/* HFI_PROP_UBWC_HBB */
	payload = core->platform->data.ubwc_config->highest_bank_bit;
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_UBWC_HBB,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_U32,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_sys_init;
	num_packets++;

	/* HFI_PROP_UBWC_BANK_SWZL_LEVEL1 */
	payload = core->platform->data.ubwc_config->bank_swzl_level;
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_UBWC_BANK_SWZL_LEVEL1,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_U32,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_sys_init;
	num_packets++;

	/* HFI_PROP_UBWC_BANK_SWZL_LEVEL2 */
	payload = core->platform->data.ubwc_config->bank_swz2_level;
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_UBWC_BANK_SWZL_LEVEL2,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_U32,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_sys_init;
	num_packets++;

	/* HFI_PROP_UBWC_BANK_SWZL_LEVEL3 */
	payload = core->platform->data.ubwc_config->bank_swz3_level;
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_UBWC_BANK_SWZL_LEVEL3,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_U32,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_sys_init;
	num_packets++;

	/* HFI_PROP_UBWC_BANK_SPREADING */
	payload = core->platform->data.ubwc_config->bank_spreading;
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_UBWC_BANK_SPREADING,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_U32,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_sys_init;
	num_packets++;

	rc = hfi_create_header(pkt, 0 /*session_id*/,
				   core->header_id++,
				   num_packets,
				   offset);

	if (rc)
		goto err_sys_init;

	d_vpr_h("System init packet created\n");
	return rc;

err_sys_init:
	d_vpr_e("%s: create packet failed\n", __func__);
	return rc;
}

int hfi_packet_image_version(struct msm_vidc_core *core,
	u8 *pkt, u32 pkt_size)
{
	int rc = 0;
	u32 num_packets = 0, offset = 0;

	if (!core || !pkt) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	/* HFI_PROP_IMAGE_VERSION */
	offset = sizeof(struct hfi_header);
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_IMAGE_VERSION,
				   (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
				   HFI_HOST_FLAGS_INTR_REQUIRED |
				   HFI_HOST_FLAGS_GET_PROPERTY),
				   HFI_PAYLOAD_NONE,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   NULL, 0);
	if (rc)
		goto err_img_version;
	num_packets++;

	rc = hfi_create_header(pkt, 0 /*session_id*/,
				   core->header_id++,
				   num_packets,
				   offset);

	if (rc)
		goto err_img_version;

	d_vpr_h("Image version packet created\n");
	return rc;

err_img_version:
	d_vpr_e("%s: create packet failed\n", __func__);
	return rc;
}

int hfi_packet_sys_pc_prep(struct msm_vidc_core *core,
	u8 *pkt, u32 pkt_size)
{
	int rc = 0;
	u32 num_packets = 0, offset = 0;

	if (!core || !pkt) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	/* HFI_CMD_POWER_COLLAPSE */
	offset = sizeof(struct hfi_header);
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_CMD_POWER_COLLAPSE,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_NONE,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   NULL, 0);
	if (rc)
		goto err_sys_pc;
	num_packets++;

	rc = hfi_create_header(pkt, 0 /*session_id*/,
				   core->header_id++,
				   num_packets,
				   offset);

	if (rc)
		goto err_sys_pc;

	d_vpr_h("Power collapse packet created\n");
	return rc;

err_sys_pc:
	d_vpr_e("%s: create packet failed\n", __func__);
	return rc;
}

int hfi_packet_sys_debug_config(struct msm_vidc_core *core,
	u8 *pkt, u32 pkt_size, u32 debug_config)
{
	int rc = 0;
	u32 num_packets = 0, offset = 0, payload = 0;

	if (!core || !pkt) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	/* HFI_PROP_DEBUG_CONFIG */
	offset = sizeof(struct hfi_header);
	payload = debug_config; /*TODO:Change later*/
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_DEBUG_CONFIG,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_U32_ENUM,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_debug;
	num_packets++;

	/* HFI_PROP_DEBUG_LOG_LEVEL */
	payload = debug_config; /*TODO:Change later*/
	rc = hfi_create_packet(pkt, pkt_size, &offset,
				   HFI_PROP_DEBUG_LOG_LEVEL,
				   HFI_HOST_FLAGS_NONE,
				   HFI_PAYLOAD_U32_ENUM,
				   HFI_PORT_NONE,
				   core->packet_id++,
				   &payload,
				   sizeof(u32));
	if (rc)
		goto err_debug;
	num_packets++;

	rc = hfi_create_header(pkt, 0 /*session_id*/,
				   core->header_id++,
				   num_packets,
				   offset);

	if (rc)
		goto err_debug;

	d_vpr_h("Debug packet created\n");
	return rc;

err_debug:
	d_vpr_e("%s: create packet failed\n", __func__);
	return rc;
}

int hfi_packet_session_command(struct msm_vidc_inst *inst,
	u32 pkt_type, u32 flags, u32 port, u32 session_id,
	u32 payload_type, void *payload, u32 payload_size)
{
	int rc = 0;
	u32 num_packets = 0, offset = 0;
	struct msm_vidc_core *core;

	if (!inst || !inst->core) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	offset = sizeof(struct hfi_header);
	rc = hfi_create_packet(inst->packet,
				inst->packet_size,
				&offset,
				pkt_type,
				flags,
				payload_type,
				port,
				core->packet_id++,
				payload,
				payload_size);
	if (rc)
		goto err_cmd;
	num_packets++;

	rc = hfi_create_header(inst->packet, session_id,
				   core->header_id++,
				   num_packets,
				   offset);

	if (rc)
		goto err_cmd;

	d_vpr_h("Command packet 0x%x created\n", pkt_type);
	return rc;

err_cmd:
	d_vpr_e("%s: create packet failed\n", __func__);
	return rc;
}

int hfi_packet_session_property(struct msm_vidc_inst *inst,
	u32 pkt_type, u32 flags, u32 port, u32 payload_type,
	void *payload, u32 payload_size)
{
	int rc = 0;
	u32 num_packets = 0, offset = 0;
	struct msm_vidc_core *core;

	if (!inst || !inst->core || !inst->packet) {
		d_vpr_e("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	core = inst->core;
	offset = sizeof(struct hfi_header);
	rc = hfi_create_packet(inst->packet, inst->packet_size,
				&offset,
				pkt_type,
				flags,
				payload_type,
				port,
				core->packet_id++,
				payload,
				payload_size);
	if (rc)
		goto err_prop;
	num_packets++;

	rc = hfi_create_header(inst->packet, inst->session_id,
				   core->header_id++,
				   num_packets,
				   offset);

	if (rc)
		goto err_prop;

	d_vpr_h("Property packet 0x%x created\n", pkt_type);
	return rc;

err_prop:
	d_vpr_e("%s: create packet failed\n", __func__);
	return rc;
}