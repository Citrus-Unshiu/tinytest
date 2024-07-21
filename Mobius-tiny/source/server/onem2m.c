#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <sys/timeb.h>
#include <limits.h>
#include "onem2m.h"
#include "dbmanager.h"
#include "httpd.h"
#include "mqttClient.h"
#include "onem2mTypes.h"
#include "config.h"
#include "util.h"
#include "cJSON.h"
#include "coap.h"

extern ResourceTree *rt;

void init_cse(cJSON *cse)
{
	char *ct = get_local_time(0);
	char *csi = (char *)malloc(strlen(CSE_BASE_RI) + 2);
	sprintf(csi, "/%s", CSE_BASE_RI);

	cJSON *srt = cJSON_CreateArray();
	cJSON_AddItemToArray(srt, cJSON_CreateNumber(RT_ACP));
	cJSON_AddItemToArray(srt, cJSON_CreateNumber(RT_AE));
	cJSON_AddItemToArray(srt, cJSON_CreateNumber(RT_CNT));
	cJSON_AddItemToArray(srt, cJSON_CreateNumber(RT_CIN));
	cJSON_AddItemToArray(srt, cJSON_CreateNumber(RT_CSE));
	cJSON_AddItemToArray(srt, cJSON_CreateNumber(RT_GRP));
	cJSON_AddItemToArray(srt, cJSON_CreateNumber(RT_CSR));
	cJSON_AddItemToArray(srt, cJSON_CreateNumber(RT_SUB));
	cJSON_AddItemToArray(srt, cJSON_CreateNumber(RT_CBA));
	cJSON_AddItemToArray(srt, cJSON_CreateNumber(RT_AEA));

	cJSON_AddStringToObject(cse, "ct", ct);
	cJSON_AddStringToObject(cse, "ri", CSE_BASE_RI);
	cJSON_AddStringToObject(cse, "lt", ct);
	cJSON_AddStringToObject(cse, "rn", CSE_BASE_NAME);
	cJSON_AddNumberToObject(cse, "cst", SERVER_TYPE);
	cJSON_AddItemToObject(cse, "srt", srt);

	cJSON *srv = cJSON_CreateArray();
	cJSON_AddItemToArray(srv, cJSON_CreateString("2a"));

	cJSON_AddItemToObject(cse, "srv", srv);
	cJSON_AddItemToObject(cse, "pi", cJSON_CreateNull());
	cJSON_AddNumberToObject(cse, "ty", RT_CSE);
	cJSON_AddStringToObject(cse, "uri", CSE_BASE_NAME);
	cJSON_AddStringToObject(cse, "csi", csi);
	cJSON_AddBoolToObject(cse, "rr", true);

	// TODO - add acpi, poa

	free(ct);
	ct = NULL;
	free(csi);
	csi = NULL;
}

void init_csr(cJSON *csr)
{
	char buf[256] = {0};

	sprintf(buf, "/%s/%s", CSE_BASE_RI, CSE_BASE_NAME);
	cJSON_AddItemToObject(csr, "cb", cJSON_CreateString(buf));
	cJSON *dcse = cJSON_CreateArray();
	//cJSON_AddItemToObject(csr, "dcse", dcse);

	cJSON *csz = cJSON_CreateArray();
	cJSON_AddItemToArray(csz, cJSON_CreateString("application/json"));

	cJSON_SetIntValue(cJSON_GetObjectItem(csr, "ty"), RT_CSR);
	cJSON_SetValuestring(cJSON_GetObjectItem(csr, "rn"), CSE_BASE_RI);
	cJSON_DeleteItemFromObject(csr, "ri");
	cJSON_DeleteItemFromObject(csr, "lt");
	cJSON_DeleteItemFromObject(csr, "ct");
	cJSON_DeleteItemFromObject(csr, "et");
	cJSON_DeleteItemFromObject(csr, "srt");
	cJSON_DeleteItemFromObject(csr, "ty");
	cJSON_DeleteItemFromObject(csr, "pi");
	cJSON_DeleteItemFromObject(csr, "at");
	cJSON_DeleteItemFromObject(csr, "aa");
}

void add_general_attribute(cJSON *root, RTNode *parent_rtnode, ResourceType ty)
{
	char *ct = get_local_time(0);
	char *ptr = NULL;

	cJSON_AddNumberToObject(root, "ty", ty);
	cJSON_AddStringToObject(root, "ct", ct);

	ptr = resource_identifier(ty, ct);
	cJSON_AddStringToObject(root, "ri", ptr);

	if (cJSON_GetObjectItem(root, "rn") == NULL)
	{
		cJSON_AddStringToObject(root, "rn", ptr);
	}
	free(ptr);
	ptr = NULL;

	cJSON_AddStringToObject(root, "lt", ct);

	cJSON *pi = cJSON_GetObjectItem(parent_rtnode->obj, "ri");
	cJSON_AddStringToObject(root, "pi", pi->valuestring);

	ptr = get_local_time(DEFAULT_EXPIRE_TIME);
	cJSON_AddStringToObject(root, "et", ptr);
	free(ptr);
	ptr = NULL;
	free(ct);
}

RTNode *create_rtnode(cJSON *obj, ResourceType ty)
{
	RTNode *rtnode = (RTNode *)calloc(1, sizeof(RTNode));
	cJSON *uri = NULL;

	rtnode->ty = ty;
	rtnode->obj = obj;

	rtnode->parent = NULL;
	rtnode->child = NULL;
	rtnode->subs = NULL;
	rtnode->sibling_left = NULL;
	rtnode->sibling_right = NULL;
	rtnode->sub_cnt = 0;
	if ((uri = cJSON_GetObjectItem(obj, "uri")))
	{
		rtnode->uri = strdup(uri->valuestring);
		cJSON_DeleteItemFromObject(obj, "uri");
	}

	rtnode->memberOf = (NodeList *)calloc(1, sizeof(NodeList));
	rtnode->memberOf->next = NULL;
	rtnode->memberOf->rtnode = NULL;
	rtnode->memberOf->uri = NULL;

	rtnode->rn = strdup(cJSON_GetObjectItem(obj, "rn")->valuestring);

	return rtnode;
}

int update_cb(cJSON *csr, cJSON *cb)
{
	cJSON *dcse = cJSON_GetObjectItem(csr, "dcse");
	cJSON *dcse_item = NULL;
	cJSON *cb_item = NULL;
	int idx = 0;
	cJSON_ArrayForEach(dcse_item, dcse)
	{
		if ((cb_item = cJSON_GetObjectItem(dcse_item, "cb")))
		{
			if (strcmp(cb_item->valuestring, cb->valuestring) == 0)
			{
				cJSON_DeleteItemFromArray(dcse, idx);
				break;
			}
		}
		idx++;
	}
	cJSON_AddItemToArray(dcse, cb);
	return 1;
}

/**
 * @brief update cnt on cin change
 * @param cnt_rtnode rtnode of cnt
 * @param cin_rtnode rtnode of cin
 * @param sign 1 for create, -1 for delete
 * @return int 1 for success, -1 for fail
 */
int update_cnt_cin(RTNode *cnt_rtnode, RTNode *cin_rtnode, int sign)
{
	if (!cnt_rtnode || !cin_rtnode)
		return -1;
	cJSON *cnt = cnt_rtnode->obj;
	cJSON *cin = cin_rtnode->obj;
	if (!cnt || !cin)
		return -1;
	cJSON *cni = cJSON_GetObjectItem(cnt, "cni");
	cJSON *cbs = cJSON_GetObjectItem(cnt, "cbs");
	cJSON *st = cJSON_GetObjectItem(cnt, "st");

	cJSON_SetIntValue(cni, cni->valueint + sign);
	cJSON_SetIntValue(cbs, cbs->valueint + (sign * cJSON_GetObjectItem(cin, "cs")->valueint));
	cJSON_SetIntValue(st, st->valueint + 1);
	logger("O2", LOG_LEVEL_DEBUG, "cni: %d, cbs: %d, st: %d", cni->valueint, cbs->valueint, st->valueint);
	delete_cin_under_cnt_mni_mbs(cnt_rtnode);

	db_update_resource(cnt, get_ri_rtnode(cnt_rtnode), RT_CNT);

	return 1;
}

int delete_onem2m_resource(oneM2MPrimitive *o2pt, RTNode *target_rtnode)
{
	logger("O2M", LOG_LEVEL_INFO, "Delete oneM2M resource");
	if (target_rtnode->ty == RT_CSE)
	{
		handle_error(o2pt, RSC_OPERATION_NOT_ALLOWED, "CSE can not be deleted");
		return RSC_OPERATION_NOT_ALLOWED;
	}
	if (check_privilege(o2pt, target_rtnode, ACOP_DELETE) == -1)
	{
		return o2pt->rsc;
	}
	cJSON *root = NULL, *lim = NULL, *ofst = NULL, *lvl = NULL;
	lim = cJSON_GetObjectItem(o2pt->fc, "lim");
	ofst = cJSON_GetObjectItem(o2pt->fc, "ofst");
	lvl = cJSON_GetObjectItem(o2pt->fc, "lvl");
	switch (o2pt->rcn)
	{
	case RCN_ATTRIBUTES:
		break;
	case RCN_CHILD_RESOURCES:
		root = cJSON_CreateObject();

		build_rcn8(o2pt, target_rtnode, root, ofst ? ofst->valueint : 0, lim ? lim->valueint : DEFAULT_DISCOVERY_LIMIT, lvl ? lvl->valueint : 10000);
		break;
	case RCN_ATTRIBUTES_AND_CHILD_RESOURCES:
		root = cJSON_CreateObject();
		lim = cJSON_GetObjectItem(o2pt->fc, "lim");
		ofst = cJSON_GetObjectItem(o2pt->fc, "ofst");
		build_rcn4(o2pt, target_rtnode, cJSON_AddObjectToObject(root, get_resource_key(target_rtnode->ty)),
				   ofst ? ofst->valueint : 0, lim ? lim->valueint : DEFAULT_DISCOVERY_LIMIT, lvl ? lvl->valueint : 10000);
		break;
	case RCN_CHILD_RESOURCE_REFERENCES:
		break;
	}
	if (root)
	{
		o2pt->response_pc = root;
	}

	delete_process(o2pt, target_rtnode);

	db_delete_onem2m_resource(target_rtnode);

	if (target_rtnode->ty == RT_CIN)
	{
		cJSON *pjson = cJSON_GetObjectItem(target_rtnode->parent->obj, "cni");
		if (pjson && pjson->valueint > 0)
		{
			cJSON_Delete(target_rtnode->obj);
			target_rtnode->obj = db_get_cin_laol(target_rtnode->parent, 0);
		}
	}
	else
	{
		free_rtnode(target_rtnode);
	}

	target_rtnode = NULL;
	o2pt->rsc = RSC_DELETED;
	return RSC_DELETED;
}

int delete_process(oneM2MPrimitive *o2pt, RTNode *rtnode)
{
	cJSON *csr, *dcse, *dcse_item;
	cJSON *pjson = NULL;
	RTNode *ptr_rtnode = NULL;
	if (rtnode->child)
	{
		ptr_rtnode = rtnode->child;
		while (ptr_rtnode)
		{
			delete_process(o2pt, ptr_rtnode);
			ptr_rtnode = ptr_rtnode->sibling_right;
		}
	}
	logger("O2M", LOG_LEVEL_DEBUG, "delete_process [%s]", rtnode->uri);
	switch (rtnode->ty)
	{
	case RT_ACP:
		break;
	case RT_AE:
		break;
	case RT_CNT:
		break;
	case RT_CIN:
		update_cnt_cin(rtnode->parent, rtnode, -1);
		break;
	case RT_SUB:
		detach_subs(rtnode->parent, rtnode);
		break;
	case RT_CSR:
		csr = rtnode->obj;
		dcse = cJSON_GetObjectItem(rt->cb->obj, "dcse");
		dcse_item = NULL;
		int idx = 0;
		cJSON_ArrayForEach(dcse_item, dcse)
		{
			if (strcmp(dcse_item->valuestring, cJSON_GetObjectItem(csr, "csi")->valuestring) == 0)
			{
				break;
			}
			idx++;
		}
		if (idx < cJSON_GetArraySize(dcse))
			cJSON_DeleteItemFromArray(dcse, idx);
		update_remote_csr_dcse(rtnode);
		logger("O2M", LOG_LEVEL_DEBUG, "delete csr");
		detach_csrlist(rtnode);
		break;
	case RT_GRP:
		cJSON_ArrayForEach(pjson, cJSON_GetObjectItem(rtnode->obj, "mid"))
		{
			ptr_rtnode = find_rtnode(pjson->valuestring);
			if (ptr_rtnode)
			{
				logger("O2M", LOG_LEVEL_DEBUG, "delete[%s] memberOf %s", ptr_rtnode->uri, pjson->valuestring);
				deleteNodeList(ptr_rtnode->memberOf, rtnode);
			}
		}
		break;
	case RT_AEA:
		break;
	case RT_CBA:
		break;
	}

	NodeList *nl = NULL;
	int idx = -1;
	nl = rtnode->memberOf->next;
	if (nl)
	{
		while (nl)
		{
			logger("O2M", LOG_LEVEL_DEBUG, "delete[%s] memberOf %s", rtnode->uri, nl->uri);
			if (!nl->rtnode)
			{
				nl = nl->next;
				continue;
			}
			pjson = cJSON_GetObjectItem(nl->rtnode->obj, "mid");
			idx = cJSON_getArrayIdx(pjson, rtnode->uri);
			if (idx >= 0)
			{
				cJSON_DeleteItemFromArray(pjson, idx);
				cJSON_SetIntValue(cJSON_GetObjectItem(nl->rtnode->obj, "cnm"), cJSON_GetArraySize(pjson));
			}
			// logger("O2M", LOG_LEVEL_DEBUG, "new mid =  %s", cJSON_Print(pjson));

			db_update_resource(nl->rtnode->obj, cJSON_GetObjectItem(nl->rtnode->obj, "ri")->valuestring, nl->rtnode->ty);
			nl = nl->next;
		}
	}

	Operation op = o2pt->op;
	o2pt->op = OP_DELETE;
	notify_onem2m_resource(o2pt, rtnode);
	o2pt->op = op;

	cJSON *at_list = cJSON_GetObjectItem(rtnode->obj, "at");
	cJSON *at = NULL;

	cJSON_ArrayForEach(at, at_list)
	{
		oneM2MPrimitive *o2pt = (oneM2MPrimitive *)calloc(1, sizeof(oneM2MPrimitive));
		o2pt->op = OP_DELETE;
		o2pt->to = strdup(at->valuestring);
		o2pt->fr = "/" CSE_BASE_RI;
		o2pt->ty = RT_AE;
		o2pt->rqi = strdup("deannounce");
		o2pt->isForwarding = true;

		forwarding_onem2m_resource(o2pt, find_csr_rtnode_by_uri(at->valuestring));
	}

	return 1;
}

int cJSON_getArrayIdx(cJSON *arr, char *value)
{
	cJSON *item = NULL;
	int idx = 0;
	cJSON_ArrayForEach(item, arr)
	{
		if (strcmp(item->valuestring, value) == 0)
		{
			return idx;
		}
		idx++;
	}
	return -1;
}

void free_rtnode(RTNode *rtnode)
{
	if (rtnode->uri)
	{
		free(rtnode->uri);
		rtnode->uri = NULL;
	}
	if (rtnode->rn)
	{
		free(rtnode->rn);
		rtnode->rn = NULL;
	}
	if (rtnode->obj)
	{
		cJSON_Delete(rtnode->obj);
		rtnode->obj = NULL;
	}
	if (rtnode->parent && rtnode->parent->child == rtnode)
	{
		rtnode->parent->child = rtnode->sibling_right;
	}
	if (rtnode->sibling_left)
	{
		rtnode->sibling_left->sibling_right = rtnode->sibling_right;
	}
	if (rtnode->sibling_right)
	{
		rtnode->sibling_right->sibling_left = rtnode->sibling_left;
	}
	if (rtnode->child)
	{
		free_rtnode_list(rtnode->child);
		rtnode->child = NULL;
	}

	if (rtnode->subs)
	{
		free_all_nodelist(rtnode->subs);
		rtnode->subs = NULL;
	}

	if (rtnode->memberOf)
	{
		free_all_nodelist(rtnode->memberOf);
		rtnode->memberOf = NULL;
	}

	free(rtnode);
}

void free_rtnode_list(RTNode *rtnode)
{
	RTNode *right = NULL;
	while (rtnode)
	{
		right = rtnode->sibling_right;
		free_rtnode(rtnode);
		rtnode = right;
	}
}

int create_onem2m_resource(oneM2MPrimitive *o2pt, RTNode *parent_rtnode)
{
	int rsc = 0;
	char err_msg[256] = {0};
	int e = check_resource_type_invalid(o2pt);
	if (e != -1)
		e = check_payload_empty(o2pt);
	if (e != -1)
		e = check_resource_type_equal(o2pt);
	if (e != -1)
		e = check_privilege(o2pt, parent_rtnode, ACOP_CREATE);
	if (e != -1)
		e = check_rn_duplicate(o2pt, parent_rtnode);
	if (e == -1)
		return o2pt->rsc;

	if (!is_attr_valid(o2pt->request_pc, o2pt->ty, err_msg))
	{
		return handle_error(o2pt, RSC_BAD_REQUEST, err_msg);
	}

	switch (o2pt->ty)
	{
	case RT_AE:
		logger("O2M", LOG_LEVEL_INFO, "Create AE");
		rsc = create_ae(o2pt, parent_rtnode);
		break;

	case RT_CNT:
		logger("O2M", LOG_LEVEL_INFO, "Create CNT");
		rsc = create_cnt(o2pt, parent_rtnode);
		break;

	case RT_CIN:
		logger("O2M", LOG_LEVEL_INFO, "Create CIN");
		rsc = create_cin(o2pt, parent_rtnode);

		break;

	case RT_SUB:
		logger("O2M", LOG_LEVEL_INFO, "Create SUB");
		rsc = create_sub(o2pt, parent_rtnode);
		break;

	case RT_ACP:
		logger("O2M", LOG_LEVEL_INFO, "Create ACP");
		rsc = create_acp(o2pt, parent_rtnode);
		break;

	case RT_GRP:
		logger("O2M", LOG_LEVEL_INFO, "Create GRP");
		rsc = create_grp(o2pt, parent_rtnode);
		break;

	case RT_CSR:
		logger("O2M", LOG_LEVEL_INFO, "Create CSR");
		rsc = create_csr(o2pt, parent_rtnode);
		break;

	case RT_AEA:
		logger("O2M", LOG_LEVEL_INFO, "Create AEA");
		rsc = create_aea(o2pt, parent_rtnode);
		break;

	case RT_CBA:
		logger("O2M", LOG_LEVEL_INFO, "Create CBA");
		rsc = create_cba(o2pt, parent_rtnode);
		break;

	case RT_MIXED:
		handle_error(o2pt, RSC_BAD_REQUEST, "resource type error");
		rsc = o2pt->rsc;
	}
	return rsc;
}

int retrieve_onem2m_resource(oneM2MPrimitive *o2pt, RTNode *target_rtnode)
{
	int rsc = 0;
	int e = check_privilege(o2pt, target_rtnode, ACOP_RETRIEVE);
	if (e == -1)
		return o2pt->rsc;

	make_response_body(o2pt, target_rtnode);

	o2pt->rsc = RSC_OK;
	return RSC_OK;
}

int update_onem2m_resource(oneM2MPrimitive *o2pt, RTNode *target_rtnode)
{
	int rsc = 0;
	char err_msg[256] = {0};
	o2pt->ty = target_rtnode->ty;
	int e = check_payload_empty(o2pt);
	ResourceType ty = parse_object_type_cjson(o2pt->request_pc);
	if (e != -1)
		e = check_resource_type_equal(o2pt);
	if (e != -1)
		e = check_privilege(o2pt, target_rtnode, ACOP_UPDATE);
	if (e != -1)
		e = check_rn_duplicate(o2pt, target_rtnode->parent);
	if (e == -1)
		return o2pt->rsc;

	if (!is_attr_valid(o2pt->request_pc, o2pt->ty, err_msg))
	{
		return handle_error(o2pt, RSC_BAD_REQUEST, err_msg);
	}

	switch (ty)
	{
	case RT_AE:
		logger("O2M", LOG_LEVEL_INFO, "Update AE");
		rsc = update_ae(o2pt, target_rtnode);
		break;

	case RT_CNT:
		logger("O2M", LOG_LEVEL_INFO, "Update CNT");
		rsc = update_cnt(o2pt, target_rtnode);
		break;

	case RT_CIN:
		logger("O2M", LOG_LEVEL_INFO, "Update CIN");
		rsc = handle_error(o2pt, RSC_OPERATION_NOT_ALLOWED, "operation `update` for cin is not allowed");
		break;

	case RT_SUB:
		logger("O2M", LOG_LEVEL_INFO, "Update SUB");
		rsc = update_sub(o2pt, target_rtnode);
		break;

	case RT_ACP:
		logger("O2M", LOG_LEVEL_INFO, "Update ACP");
		rsc = update_acp(o2pt, target_rtnode);
		break;

	case RT_GRP:
		logger("O2M", LOG_LEVEL_INFO, "Update GRP");
		rsc = update_grp(o2pt, target_rtnode);
		break;

	case RT_AEA:
		logger("O2M", LOG_LEVEL_INFO, "Update AEA");
		rsc = update_aea(o2pt, target_rtnode);
		break;

	case RT_CSR:
		logger("O2M", LOG_LEVEL_INFO, "Update CSR");
		rsc = update_csr(o2pt, target_rtnode);
		break;

		// case RT_CBA:
		// 	logger("O2M", LOG_LEVEL_INFO, "Update CBA");
		// 	rsc = update_cba(o2pt, target_rtnode);
		// 	break;

	default:
		handle_error(o2pt, RSC_OPERATION_NOT_ALLOWED, "operation `update` is unsupported");
		rsc = o2pt->rsc;
	}
	announce_to_annc(target_rtnode);
	return rsc;
}

int fopt_onem2m_resource(oneM2MPrimitive *o2pt, RTNode *parent_rtnode)
{
	int rsc = 0;
	int cnt = 0;
	int cnm = 0;
	int idx = 1;
	bool updated = false;

	RTNode *target_rtnode = NULL;
	oneM2MPrimitive *req_o2pt = NULL;
	cJSON *new_pc = NULL;
	cJSON *agr = NULL;
	cJSON *rsp = NULL;
	cJSON *json = NULL;
	cJSON *grp = NULL;
	cJSON *delete_list = NULL;

	if (parent_rtnode == NULL)
	{
		o2pt->rsc = RSC_NOT_FOUND;
		return RSC_NOT_FOUND;
	}
	logger("O2M", LOG_LEVEL_DEBUG, "handle fopt");

	if (cJSON_GetObjectItem(parent_rtnode->obj, "macp"))
	{
		if (check_macp_privilege(o2pt, parent_rtnode, op_to_acop(o2pt->op)) == -1)
		{
			return o2pt->rsc;
		}
	}
	else if (cJSON_GetObjectItem(parent_rtnode->obj, "acpi"))
	{
		if (check_privilege(o2pt, parent_rtnode, op_to_acop(o2pt->op)) == -1)
		{
			return o2pt->rsc;
		}
	}

	grp = parent_rtnode->obj;
	if (!grp)
	{
		o2pt->rsc = RSC_INTERNAL_SERVER_ERROR;
		return RSC_INTERNAL_SERVER_ERROR;
	}

	if ((cnm = cJSON_GetObjectItem(grp, "cnm")->valueint) == 0)
	{
		logger("O2M", LOG_LEVEL_DEBUG, "No member to fanout");
		return o2pt->rsc = RSC_NO_MEMBERS;
	}

	o2ptcpy(&req_o2pt, o2pt);

	new_pc = cJSON_CreateObject();
	cJSON_AddItemToObject(new_pc, "m2m:agr", agr = cJSON_CreateObject());
	cJSON_AddItemToObject(agr, "m2m:rsp", rsp = cJSON_CreateArray());
	cJSON *mid_arr = cJSON_GetObjectItem(grp, "mid");
	cJSON *mid_obj = NULL;
	cJSON *mid_prev = NULL;
	cJSON_InsertItemInArray(mid_arr, 0, cJSON_CreateString(""));
	cJSON_AddItemToArray(mid_arr, cJSON_CreateString(""));

	cJSON_ArrayForEach(mid_obj, mid_arr)
	{
		rsc = 0;
		char *mid = cJSON_GetStringValue(mid_obj);
		if (strlen(mid) == 0)
			continue;
		if (req_o2pt->to)
			free(req_o2pt->to);
		if (o2pt->fopt)
		{
			if (strncmp(mid, CSE_BASE_NAME, strlen(CSE_BASE_NAME)) != 0)
			{
				mid = ri_to_uri(mid);
			}
			req_o2pt->to = malloc(strlen(mid) + strlen(o2pt->fopt) + 2);
			sprintf(req_o2pt->to, "%s%s", mid, o2pt->fopt);
		}
		else
		{
			req_o2pt->to = malloc(strlen(mid) + 2);
			sprintf(req_o2pt->to, "%s", mid);
		}
		req_o2pt->isFopt = false;
		ResourceAddressingType RAT = checkResourceAddressingType(mid);
		if (RAT == CSE_RELATIVE)
		{
			target_rtnode = find_rtnode(req_o2pt->to);
		}
		else if (RAT == SP_RELATIVE)
		{
			if (isSpRelativeLocal(req_o2pt->to))
			{
				target_rtnode = find_rtnode(req_o2pt->to);
			}
			else
			{
				target_rtnode = get_remote_resource(req_o2pt->to, &rsc);
				if (!target_rtnode)
				{
					req_o2pt->rsc = rsc;
				}
			}
		}
		mid_prev = mid_obj->prev;
		if (target_rtnode)
		{
			rsc = handle_onem2m_request(req_o2pt, target_rtnode);
			if (rsc < 4000)
				cnt++;
			json = o2pt_to_json(req_o2pt);
			if (json)
			{
				cJSON_AddItemToArray(rsp, json);
			}
			if (req_o2pt->op != OP_DELETE && target_rtnode->ty == RT_CIN)
			{
				free_rtnode(target_rtnode);
				target_rtnode = NULL;
			}
			if (RAT == SP_RELATIVE && !isSpRelativeLocal(mid))
			{
				free_rtnode(target_rtnode);
				target_rtnode = NULL;
			}

			logger("O2M", LOG_LEVEL_DEBUG, "rsc : %d", rsc);
		}
		else
		{
			logger("O2M", LOG_LEVEL_DEBUG, "rtnode not found");
			if (RAT == SP_RELATIVE && !isSpRelativeLocal(mid))
			{
				cJSON_AddItemToArray(rsp, o2pt_to_json(req_o2pt));
			}
		}
		if (rsc == RSC_DELETED)
		{
			updated = true;
			mid_obj = mid_prev;
			// cJSON_DeleteItemFromArray(mid_arr, idx);
			// idx--;
		}
		// idx++;
	}
	cJSON_DeleteItemFromArray(mid_arr, 0);
	cJSON_DeleteItemFromArray(mid_arr, cJSON_GetArraySize(mid_arr) - 1);
	if (updated)
	{
		db_update_resource(grp, cJSON_GetObjectItem(grp, "ri")->valuestring, RT_GRP);
	}

	o2pt->response_pc = new_pc;

	cJSON_Delete(delete_list);

	o2pt->rsc = RSC_OK;

	free_o2pt(req_o2pt);
	req_o2pt = NULL;
	return RSC_OK;
}

/**
 * Discover Resources based on Filter Criteria
 */
int discover_onem2m_resource(oneM2MPrimitive *o2pt, RTNode *target_rtnode)
{
	logger("MAIN", LOG_LEVEL_DEBUG, "Discover Resource");
	cJSON *fc = o2pt->fc;
	cJSON *pjson = NULL, *pjson2 = NULL;
	cJSON *acpi_obj = NULL;
	cJSON *root = cJSON_CreateObject();
	cJSON *uril = NULL, *list = NULL;
	cJSON *json = NULL;

	int lSize = 0;
	if (check_privilege(o2pt, target_rtnode, ACOP_DISCOVERY) == -1)
	{
		uril = cJSON_CreateArray();
		cJSON_AddItemToObject(root, "m2m:uril", uril);
		o2pt->response_pc = root;
		return RSC_OK;
	}

	if (!o2pt->fc)
	{
		logger("O2M", LOG_LEVEL_WARN, "Empty Filter Criteria");
		return RSC_BAD_REQUEST;
	}

	list = db_get_filter_criteria(o2pt);
	lSize = cJSON_GetArraySize(list);
	cJSON *lim_obj = cJSON_GetObjectItem(fc, "lim");
	cJSON *ofst_obj = cJSON_GetObjectItem(fc, "ofst");
	int lim = INT_MAX;
	int ofst = 0;
	if (lim_obj)
	{
		lim = cJSON_GetNumberValue(lim_obj);
	}
	else
	{
		lim = DEFAULT_DISCOVERY_LIMIT;
	}
	if (ofst_obj)
	{
		ofst = cJSON_GetNumberValue(ofst_obj);
	}

	if (lSize > lim)
	{
		logger("O2M", LOG_LEVEL_DEBUG, "limit exceeded");
		cJSON_DeleteItemFromArray(list, lSize);
		o2pt->cnst = CS_PARTIAL_CONTENT;
		o2pt->cnot = ofst + lim;
	}

	if (o2pt->drt == DRT_STRUCTURED)
		cJSON_AddItemToObject(root, "m2m:uril", list);
	else
		cJSON_AddItemToObject(root, "m2m:rrl", list);

	o2pt->response_pc = root;

	return o2pt->rsc = RSC_OK;
}

int notify_onem2m_resource(oneM2MPrimitive *o2pt, RTNode *target_rtnode)
{
	logger("O2M", LOG_LEVEL_DEBUG, "Notify onem2m Resource [%s]", target_rtnode->uri);
	cJSON *pjson = NULL;
	NodeList *del = NULL;
	NodeList *node = NULL;

	if (!target_rtnode)
	{
		logger("O2M", LOG_LEVEL_ERROR, "target_rtnode is NULL");
		return -1;
	}
	if (o2pt->ty == RT_SUB && (o2pt->op == OP_CREATE || o2pt->op == OP_UPDATE))
	{
		return 1;
	}

	if (target_rtnode->ty != RT_SUB && target_rtnode->sub_cnt == 0)
	{
		return 1;
	}
	int net = NET_NONE;

	switch (o2pt->op)
	{
	case OP_CREATE:
		net = NET_CREATE_OF_DIRECT_CHILD_RESOURCE;
		break;
	case OP_UPDATE:
		net = NET_UPDATE_OF_RESOURCE;
		break;
	case OP_DELETE:
		net = NET_DELETE_OF_RESOURCE;
		break;
	}
	logger("O2M", LOG_LEVEL_DEBUG, "Net : %d", net);
	cJSON *noti_cjson, *sgn, *nev, *nct, *exc;
	noti_cjson = cJSON_CreateObject();
	cJSON_AddItemToObject(noti_cjson, "m2m:sgn", sgn = cJSON_CreateObject());
	cJSON_AddItemToObject(sgn, "nev", nev = cJSON_CreateObject());
	cJSON_AddNumberToObject(nev, "net", net);

	if (target_rtnode->sub_cnt > 0)
	{
		node = target_rtnode->subs;
	}

	while (node)
	{
		if (!isAptEnc(o2pt, target_rtnode, node->rtnode))
		{
			logger("O2M", LOG_LEVEL_DEBUG, "skip notification");
			node = node->next;
			continue;
		}

		cJSON *enc = cJSON_GetObjectItem(node->rtnode->obj, "enc");
		exc = cJSON_GetObjectItem(node->rtnode->obj, "exc");
		cJSON_AddStringToObject(sgn, "sur", node->rtnode->uri);
		if (node->rtnode->obj && (nct = cJSON_GetObjectItem(node->rtnode->obj, "nct")))
		{
			switch (nct->valueint)
			{
			case NCT_ALL_ATTRIBUTES:
				cJSON_AddItemReferenceToObject(nev, "rep", o2pt->response_pc);
				break;
			case NCT_MODIFIED_ATTRIBUTES:
				cJSON_AddItemToObject(nev, "rep", cJSON_Duplicate(o2pt->request_pc, true));
				break;
			case NCT_RESOURCE_ID:
				cJSON_AddItemToObject(nev, "rep", pjson = cJSON_CreateObject());
				cJSON_AddItemToObject(pjson, "m2m:uri", cJSON_CreateString(target_rtnode->uri));
				break;
			case NCT_TRIGGER_PAYLOAD:
				break;
			case NCT_TIMESERIES_NOTIFICATION:
				break;
			}
		}

		if ((pjson = cJSON_GetObjectItem(node->rtnode->obj, "cr")))
		{
			cJSON_AddItemReferenceToObject(sgn, "cr", pjson);
		}

		cJSON *net_obj = cJSON_GetObjectItem(cJSON_GetObjectItem(node->rtnode->obj, "enc"), "net");

		if (net_obj)
		{
			cJSON_ArrayForEach(pjson, net_obj)
			{
				if (pjson->valueint == net)
				{
					// logger("O2M", LOG_LEVEL_DEBUG, "notify to nu \n%s", cJSON_Print(noti_cjson));
					notify_to_nu(node->rtnode, noti_cjson, net);
					cJSON_SetNumberValue(exc, exc->valueint - 1);
					break;
				}
			}
		}
		cJSON_DeleteItemFromObject(sgn, "sur");
		cJSON_DeleteItemFromObject(sgn, "cr");

		if (exc)
		{
			logger("O2M", LOG_LEVEL_DEBUG, "exc : %d", exc->valueint);
			if (exc->valueint == 0)
			{
				del = node;
				node = node->next;
				logger("O2M", LOG_LEVEL_DEBUG, "delete_process %s", del->uri);
				delete_process(o2pt, del->rtnode);
				logger("O2M", LOG_LEVEL_DEBUG, "delete_process done");
				logger("O2M", LOG_LEVEL_DEBUG, "%s", del->uri);
				db_delete_onem2m_resource(del->rtnode);
				logger("O2M", LOG_LEVEL_DEBUG, "db_delete_onem2m_resource done");
				free_rtnode(del->rtnode);
				del = NULL;
				continue;
			}
			else
			{
				db_update_resource(node->rtnode->obj, get_ri_rtnode(node->rtnode), node->rtnode->ty);
			}
		}
		node = node->next;
	}
	node = NULL;

	if (target_rtnode->ty == RT_SUB && net == NET_DELETE_OF_RESOURCE)
	{
		logger("O2M", LOG_LEVEL_DEBUG, "notify delete sub");
		cJSON_AddItemToObject(nev, "sur", cJSON_CreateString(target_rtnode->uri));
		cJSON_AddItemToObject(sgn, "sud", cJSON_CreateBool(true));
		notify_to_nu(target_rtnode, noti_cjson, net);
		cJSON_DeleteItemFromObject(sgn, "sud");
	}

	if (net == NET_DELETE_OF_RESOURCE)
	{
		logger("O2M", LOG_LEVEL_DEBUG, "notify delete child resource");
		net = NET_DELETE_OF_DIRECT_CHILD_RESOURCE;
		cJSON_SetNumberValue(cJSON_GetObjectItem(nev, "net"), net);
		if (target_rtnode->parent->sub_cnt > 0)
			node = target_rtnode->parent->subs;

		while (node)
		{
			cJSON_AddStringToObject(sgn, "sur", node->rtnode->uri);
			notify_to_nu(node->rtnode, noti_cjson, net);
			cJSON_DeleteItemFromObject(sgn, "sur");
			node = node->next;
		}
	}

	cJSON_Delete(noti_cjson);

	return 1;
}

/**
 * creating remote annc
 * @param parent_rtnode parent resource node
 * @param obj resource object
 * @param at announceTo string
 * @param isParent true if parent resource is parent resource
 */
char *create_remote_annc(RTNode *parent_rtnode, cJSON *obj, char *at, bool isParent)
{
	extern cJSON *ATTRIBUTES;
	char buf[256] = {0};
	bool pannc = false;

	// Check Parent Resource has attribute at
	cJSON *pat = cJSON_GetObjectItem(parent_rtnode->obj, "at");
	int ty = cJSON_GetObjectItem(obj, "ty")->valueint;
	pannc = false;
	char *parent_target = NULL;
	cJSON *pjson = NULL;
	ResourceAddressingType RAT = checkResourceAddressingType(at);
	cJSON_ArrayForEach(pjson, pat)
	{
		if (!strncmp(pjson->valuestring, at, strlen(at)) && pjson->valuestring[strlen(at)] == '/')
		{
			pannc = true;
			parent_target = strdup(pjson->valuestring);
			break;
		}
	}

	if (!pannc)
	{ // if parent resource has no attribute at
		logger("UTIL", LOG_LEVEL_DEBUG, "Announcement for %s not created", parent_rtnode->uri);
		if (parent_rtnode->parent)
		{
			logger("UTIL", LOG_LEVEL_DEBUG, "Creating remote annc for %s", parent_rtnode->parent->uri);
			parent_target = create_remote_annc(parent_rtnode->parent, parent_rtnode->obj, at, true);
			if (!parent_target)
			{
				logger("UTIL", LOG_LEVEL_ERROR, "Announcement for %s not created", parent_rtnode->uri);
				return NULL;
			}
		}
		else
		{
			logger("UTIL", LOG_LEVEL_DEBUG, "Creating cbA");
			if (create_remote_cba(at, &parent_target) == -1)
			{
				logger("UTIL", LOG_LEVEL_ERROR, "Announcement for %s not created", parent_rtnode->uri);
				return NULL;
			}
		}
	}

	if (RAT == SP_RELATIVE)
	{
		oneM2MPrimitive *o2pt = (oneM2MPrimitive *)calloc(sizeof(oneM2MPrimitive), 1);
		o2pt->fr = strdup("/" CSE_BASE_RI);
		o2pt->to = strdup(parent_target);
		o2pt->op = OP_CREATE;
		o2pt->ty = ty + 10000;
		o2pt->rqi = strdup("create-annc");
		o2pt->rvi = strdup("3");

		cJSON *root = cJSON_CreateObject();
		cJSON *annc = cJSON_CreateObject();
		cJSON *pjson = NULL;
		cJSON_AddItemToObject(root, get_resource_key(ty + 10000), annc);
		sprintf(buf, "/%s/%s/%s", CSE_BASE_RI, get_uri_rtnode(parent_rtnode), cJSON_GetObjectItem(obj, "rn")->valuestring);
		cJSON_AddItemToObject(annc, "lnk", cJSON_CreateString(buf));

		switch (ty)
		{
		case RT_ACP:
			if ((pjson = cJSON_Duplicate(cJSON_GetObjectItem(obj, "pv"), true)))
				cJSON_AddItemToObject(annc, "pv", pjson);
			if ((pjson = cJSON_Duplicate(cJSON_GetObjectItem(obj, "pvs"), true)))
				cJSON_AddItemToObject(annc, "pvs", pjson);
			break;
		case RT_AE:
			pjson = cJSON_Duplicate(cJSON_GetObjectItem(obj, "srv"), true);
			cJSON_AddItemToObject(annc, "srv", pjson);
			break;
		case RT_CNT:
			break;
		case RT_CIN:
			break;
		case RT_SUB:
			break;
		case RT_GRP:
			break;
		}

		cJSON *lbl = cJSON_GetObjectItem(obj, "lbl");
		if (lbl)
			cJSON_AddItemToObject(annc, "lbl", cJSON_Duplicate(lbl, true));

		cJSON *ast = cJSON_GetObjectItem(obj, "ast");
		if (ast)
			cJSON_AddItemToObject(annc, "ast", cJSON_Duplicate(ast, true));

		cJSON *aa = cJSON_GetObjectItem(obj, "aa");
		cJSON *attr = cJSON_GetObjectItem(ATTRIBUTES, get_resource_key(ty));
		cJSON_ArrayForEach(pjson, aa)
		{
			if (strcmp(pjson->valuestring, "lbl") == 0)
				continue;
			if (strcmp(pjson->valuestring, "ast") == 0)
				continue;
			if (strcmp(pjson->valuestring, "lnk") == 0)
				continue;
			if (!cJSON_GetObjectItem(attr, pjson->valuestring))
			{
				logger("UTIL", LOG_LEVEL_ERROR, "invalid attribute in aa");
				return NULL;
			}
			cJSON *temp = cJSON_GetObjectItem(obj, pjson->valuestring);
			cJSON_AddItemToObject(annc, pjson->valuestring, cJSON_Duplicate(temp, true));
		}

		o2pt->response_pc = root;
		o2pt->isForwarding = true;
		RTNode *rtnode = find_csr_rtnode_by_uri(at);
		if (!rtnode)
		{
			logger("UTIL", LOG_LEVEL_ERROR, "at target not found");
			return NULL;
		}
		int rsc = 0;
		if ((rsc = forwarding_onem2m_resource(o2pt, rtnode)) >= 4000)
		{
			free_o2pt(o2pt);
			logger("UTIL", LOG_LEVEL_ERROR, "Creation failed");
			return NULL;
		}

		cJSON *result = o2pt->response_pc;
		cJSON *annc_obj = cJSON_GetObjectItem(result, get_resource_key(ty + 10000));
		char *annc_ri = cJSON_GetObjectItem(annc_obj, "ri")->valuestring;
		sprintf(buf, "%s/%s", at, annc_ri);
		if (isParent)
		{
			cJSON *at = cJSON_GetObjectItem(obj, "at");
			if (!at)
			{
				at = cJSON_CreateArray();
				cJSON_AddItemToObject(obj, "at", at);
			}
			cJSON_AddItemToArray(at, cJSON_CreateString(buf));
			db_update_resource(obj, cJSON_GetObjectItem(obj, "ri")->valuestring, ty);
		}
		cJSON_Delete(result);
		free_o2pt(o2pt);
		free(parent_target);

		return strdup(buf);
	}
	return NULL;
}

/**
 * @brief forward oneM2M resource to remote CSE\nreturn stored in o2pt
 * @param o2pt oneM2M request primitive
 * @param target_rtnode target csr
 * @return int result code
 */
int forwarding_onem2m_resource(oneM2MPrimitive *o2pt, RTNode *target_rtnode)
{
	Protocol protocol = 0;
	char *host = NULL;
	int port = 0;
	char *path = NULL;
	int rsc = 0;
	char buf[256] = {0};

	logger("O2M", LOG_LEVEL_DEBUG, "Forwarding Resource");

	if (!target_rtnode)
	{
		if (rt->registrar_csr)
		{
			logger("O2M", LOG_LEVEL_DEBUG, "local csr not found, forwarding to registrar");
			target_rtnode = rt->registrar_csr;
		}
		else
		{
			return o2pt->rsc = RSC_NOT_FOUND;
		}
	}

	if (target_rtnode->ty != RT_CSR)
	{
		logger("O2M", LOG_LEVEL_ERROR, "target_rtnode is not CSR");
		return o2pt->rsc = RSC_NOT_FOUND;
	}

	if (checkResourceAddressingType(o2pt->fr) == CSE_RELATIVE)
	{
		sprintf(buf, "/%s/%s", CSE_BASE_RI, o2pt->fr);
		free(o2pt->fr);
		o2pt->fr = strdup(buf);
	}

	cJSON *csr = target_rtnode->obj;
	cJSON *poa_list = cJSON_GetObjectItem(csr, "poa");
	cJSON *poa = NULL;
	cJSON_ArrayForEach(poa, poa_list)
	{
		if (parsePoa(poa->valuestring, &protocol, &host, &port, &path) == -1)
		{
			logger("O2M", LOG_LEVEL_ERROR, "poa parse error");
			continue;
		}
		if (protocol == PROT_HTTP)
		{
			http_forwarding(o2pt, host, port);
		}

#ifdef ENABLE_MQTT
		else if (protocol == PROT_MQTT)
		{
			mqtt_forwarding(o2pt, host, port, csr);
		}
#endif

#ifdef ENABLE_COAP
		else if (protocol == PROT_COAP)
		{
			coap_forwarding(o2pt, host, port);
		}
#endif
		free(host);
		free(path);

		if (o2pt->rsc == RSC_OK)
		{
			break;
		}
	}

	if (o2pt->rsc >= 4000)
	{
		logger("O2M", LOG_LEVEL_ERROR, "forwarding failed");
		return o2pt->rsc;
	}

	return o2pt->rsc;
}