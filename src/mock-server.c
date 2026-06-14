/*
 * Embedded Jianghu OL mock-server implementation.
 *
 * This file is intentionally included by main.c instead of compiled as a
 * separate translation unit for now. The mock still depends on emulator-local
 * static helpers and runtime globals; this split only moves server behavior out
 * of the main emulator source without changing linkage or guest-visible logic.
 */

typedef struct
{
    const char *name;
    const char *contains;
    const char *responseFile;
    const u8 *response;
    u32 responseLen;
} vm_net_mock_rule;

static u8 g_netMockTitleServerListPending = 0;
static u8 g_netMockTitleServerSelectConfirmed = 0;
static u32 g_netMockTitleServerListTick = 0;
static u32 g_netMockTitleServerSelectTick = 0;
static u32 g_netMockTitleSelectedServerId = 0;

static bool vm_net_mock_request_contains(const u8 *request, u32 requestLen, const char *needle)
{
    u32 needleLen = needle ? (u32)strlen(needle) : 0;
    if (request == NULL || needleLen == 0 || requestLen < needleLen)
        return false;

    for (u32 i = 0; i + needleLen <= requestLen; ++i)
    {
        if (memcmp(request + i, needle, needleLen) == 0)
            return true;
    }
    return false;
}

static bool vm_net_mock_request_has_empty_field(const u8 *request, u32 requestLen, const char *field)
{
    u32 fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || requestLen < fieldLen + 4)
        return false;

    for (u32 i = 0; i + fieldLen + 4 <= requestLen; ++i)
    {
        if (memcmp(request + i, field, fieldLen) != 0)
            continue;
        u32 p = i + fieldLen;
        if (p + 4 <= requestLen && request[p] == 0 && request[p + 1] == 0x02 && request[p + 2] == 0 && request[p + 3] == 0)
            return true;
    }
    return false;
}

static bool vm_net_mock_get_object_u8_field(const u8 *request, u32 requestLen, const char *field, u8 *value)
{
    u32 fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 5)
        return false;

    for (u32 i = 0; i + fieldLen + 5 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        if (p < requestLen && request[p] == 0)
            p++;
        if (p + 4 <= requestLen && request[p] == 0x03 && request[p + 1] == 0 && request[p + 2] == 1)
        {
            if (value)
                *value = request[p + 3];
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_get_object_u32_field(const u8 *request, u32 requestLen, const char *field, u32 *value)
{
    u32 fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 8)
        return false;

    for (u32 i = 0; i + fieldLen + 8 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        if (p < requestLen && request[p] == 0)
            p++;
        if (p + 7 <= requestLen && request[p] == 0x06 && request[p + 1] == 0 && request[p + 2] == 4)
        {
            if (value)
                *value = ((u32)request[p + 3] << 24) | ((u32)request[p + 4] << 16) |
                         ((u32)request[p + 5] << 8) | (u32)request[p + 6];
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_get_object_blob_field(const u8 *request, u32 requestLen, const char *field,
                                              const u8 **value, u16 *valueLen)
{
    u32 fieldLen = (u32)strlen(field);
    if (value)
        *value = NULL;
    if (valueLen)
        *valueLen = 0;
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 5)
        return false;

    for (u32 i = 0; i + fieldLen + 5 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        if (p + 2 > requestLen)
            return false;
        u16 wrappedLen = (u16)(((u16)request[p] << 8) | request[p + 1]);
        p += 2;
        if (p + wrappedLen > requestLen || wrappedLen < 2)
            return false;
        u16 blobLen = (u16)(((u16)request[p] << 8) | request[p + 1]);
        p += 2;
        if ((u32)blobLen + 2 != wrappedLen || p + blobLen > requestLen)
            return false;
        if (value)
            *value = request + p;
        if (valueLen)
            *valueLen = blobLen;
        return true;
    }
    return false;
}

static bool vm_net_mock_get_object_string_field(const u8 *request, u32 requestLen, const char *field, char *value, u32 valueCap)
{
    u32 fieldLen = (u32)strlen(field);
    if (value && valueCap > 0)
        value[0] = 0;
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 5 || value == NULL || valueCap == 0)
        return false;

    for (u32 i = 0; i + fieldLen + 5 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        if (p + 2 > requestLen)
            return false;
        u16 valueLen = (u16)(((u16)request[p] << 8) | request[p + 1]);
        p += 2;
        if (p + valueLen > requestLen || valueLen < 2)
            return false;
        u16 blobLen = (u16)(((u16)request[p] << 8) | request[p + 1]);
        p += 2;
        if ((u32)blobLen + 2 != valueLen || p + blobLen > requestLen)
            return false;

        u32 copyLen = SDL_min((u32)blobLen, valueCap - 1);
        memcpy(value, request + p, copyLen);
        value[copyLen] = 0;
        return true;
    }
    return false;
}

typedef struct
{
    u8 major;
    u8 kind;
    u8 subtype;
    const u8 *payload;
    u16 payloadLen;
} vm_net_mock_request_object;

static bool vm_net_mock_next_request_object(const u8 *request, u32 requestLen, u32 *offset, vm_net_mock_request_object *object)
{
    if (request == NULL || offset == NULL || *offset < 4 || *offset + 5 > requestLen)
        return false;

    u32 objectStart = *offset;
    u16 objectLen = (u16)(((u16)request[objectStart + 3] << 8) | request[objectStart + 4]);
    if (objectLen < 5 || objectStart + objectLen > requestLen)
        return false;

    if (object)
    {
        object->major = request[objectStart];
        object->kind = request[objectStart + 1];
        object->subtype = request[objectStart + 2];
        object->payload = request + objectStart + 5;
        object->payloadLen = (u16)(objectLen - 5);
    }
    *offset = objectStart + objectLen;
    return true;
}

static bool vm_net_mock_get_first_object_kind_subtype(const u8 *request, u32 requestLen, u8 *kind, u8 *subtype)
{
    if (request == NULL || requestLen < 11)
        return false;
    if (request[0] != 'W' || request[1] != 'T' || request[4] == 0)
        return false;
    if (kind)
        *kind = request[6];
    if (subtype)
        *subtype = request[7];
    return true;
}

static bool vm_net_mock_get_wt_header_kind_subtype(const u8 *request, u32 requestLen, u8 *kind, u8 *subtype)
{
    if (request == NULL || requestLen < 8)
        return false;
    if (request[0] != 'W' || request[1] != 'T')
        return false;
    if (kind)
        *kind = request[5];
    if (subtype)
        *subtype = request[6];
    return true;
}

static bool vm_net_mock_request_contains_object(const u8 *request, u32 requestLen, u8 major, u8 kind, u8 subtype)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == major && object.kind == kind && object.subtype == subtype)
            return true;
    }
    return false;
}

static u32 vm_net_mock_min_u32(u32 a, u32 b)
{
    return (a < b) ? a : b;
}

static bool vm_net_mock_is_title_server_select_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen < 4)
        return false;

    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 1, 4))
        return false;

    /*
     * Server/area confirmation carries server-selection state on 1/1/4.
     * Do not answer this with title role-list actorinfo: mmTitle sub_3544()
     * treats actorinfo on subtype 4 as the role-list payload, which belongs
     * behind the later explicit 1/1/16 role-list request.
     */
    return vm_net_mock_request_contains(request, requestLen, "serverID") &&
           vm_net_mock_request_contains(request, requestLen, "moneytype");
}

static bool vm_net_mock_is_title_rolelist_stage_request(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    return kind == 1 &&
           subtype == 16 &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 1, 16) &&
           !vm_net_mock_is_title_server_select_request(request, requestLen);
}

static bool vm_net_mock_is_title_role_select_request(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    return kind == 1 &&
           subtype == 6 &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 1, 6) &&
           vm_net_mock_request_contains(request, requestLen, "actorID");
}

static bool vm_net_mock_is_login_validation_request(const u8 *request, u32 requestLen, u8 *requestSubtype)
{
    u8 kind = 0;
    u8 subtype = 0;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    if (kind != 1)
        return false;
    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 1, subtype))
        return false;
    if (!vm_net_mock_request_contains(request, requestLen, "coreVer") ||
        !vm_net_mock_request_contains(request, requestLen, "appVer") ||
        !(vm_net_mock_request_contains(request, requestLen, "username") ||
          vm_net_mock_request_contains(request, requestLen, "userName")) ||
        !vm_net_mock_request_contains(request, requestLen, "password") ||
        !vm_net_mock_request_contains(request, requestLen, "imsi"))
    {
        return false;
    }
    if (requestSubtype)
        *requestSubtype = subtype;
    return true;
}

static void vm_net_ensure_business_callback(const char *reason)
{
    if (g_netCurrentObject == 0)
        return;

    u32 callback = 0;
    u32 callbackAddr = g_netCurrentObject + 0x14;
    if (uc_mem_read(MTK, callbackAddr, &callback, 4) != UC_ERR_OK)
        return;
    if (callback != 0)
    {
        vm_net_trace("business_cb_existing obj=%08x cb=%08x reason=%s\n", g_netCurrentObject, callback, reason ? reason : "");
        return;
    }

    callback = VM_GAME_NET_BUSINESS_CALLBACK;
    uc_mem_write(MTK, callbackAddr, &callback, 4);
    vm_net_trace("business_cb_install obj=%08x cb=%08x reason=%s\n", g_netCurrentObject, callback, reason ? reason : "");
}

static u32 vm_net_mock_copy_response(const u8 *response, u32 responseLen, u8 *out, u32 outCap)
{
    if (response == NULL || responseLen == 0 || out == NULL || outCap == 0)
        return 0;
    u32 copyLen = responseLen < outCap ? responseLen : outCap;
    memcpy(out, response, copyLen);
    return copyLen;
}

static u32 vm_net_mock_load_response_file(const char *path, u8 *out, u32 outCap)
{
    if (path == NULL || out == NULL || outCap == 0)
        return 0;

    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    size_t len = fread(out, 1, outCap, fp);
    fclose(fp);
    return (u32)len;
}

static bool vm_net_mock_file_has_data(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    return size > 40;
}

static bool vm_net_mock_file_has_min_size(const char *path, long minSize)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    return size >= minSize;
}

static long vm_net_mock_file_size(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return -1;
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return -1;
    }
    long size = ftell(fp);
    fclose(fp);
    return size;
}

static u32 vm_net_mock_file_checksum(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;
    u32 sum = 2166136261u;
    int ch;
    while ((ch = fgetc(fp)) != EOF)
    {
        sum ^= (u8)ch;
        sum *= 16777619u;
    }
    fclose(fp);
    return sum;
}

static bool vm_host_file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    fclose(fp);
    return true;
}

static u32 vm_alloc_host_string(const char *text)
{
    u32 len = (u32)strlen(text) + 1;
    u32 ptr = vm_malloc(len);
    if (ptr)
        uc_mem_write(MTK, ptr, text, len);
    return ptr;
}

static bool vm_net_mock_has_installed_update(void)
{
    long installedSize = vm_net_mock_file_size("JHOnlineData/MMORPGTempcbm");
    long gameSize = vm_net_mock_file_size("JHOnlineData/mmGameMstarWqvga.cbm");
    if (installedSize < 41 || gameSize < 41 ||
        installedSize != gameSize ||
        !vm_net_mock_file_has_min_size("JHOnlineData/mmorpg_updateversioncbm", 40))
    {
        vm_net_trace("mock_update_installed no installedSize=%ld gameSize=%ld version=%d\n",
                     installedSize, gameSize,
                     vm_net_mock_file_has_min_size("JHOnlineData/mmorpg_updateversioncbm", 40) ? 1 : 0);
        return false;
    }

    u32 installedHash = vm_net_mock_file_checksum("JHOnlineData/MMORPGTempcbm");
    u32 gameHash = vm_net_mock_file_checksum("JHOnlineData/mmGameMstarWqvga.cbm");
    bool ok = installedHash == gameHash;
    vm_net_trace("mock_update_installed %s installedSize=%ld gameSize=%ld installedHash=%08x gameHash=%08x\n",
                 ok ? "yes" : "no", installedSize, gameSize, installedHash, gameHash);
    return ok;
}

static bool vm_net_mock_buffer_has_nonzero(const u8 *data, u32 len)
{
    for (u32 i = 0; i < len; ++i)
    {
        if (data[i] != 0)
            return true;
    }
    return false;
}

static u32 vm_net_mock_signed_byte_sum(const u8 *data, u32 len)
{
    int sum = 0;
    for (u32 i = 0; i < len; ++i)
        sum += (signed char)data[i];
    return (u32)sum;
}

static u32 vm_net_mock_read_download_checksum(void)
{
    u32 value = 0;
    if (Global_R9)
        uc_mem_read(MTK, Global_R9 + 0x9584, &value, sizeof(value));
    return value;
}

static u32 vm_net_mock_load_update_payload(u8 *out, u32 outCap)
{
    static const char *payloadPaths[] = {
        "JHOnlineData/MMORPGTempcbm.mock",
        "JHOnlineData/mmGameMstarWqvga.cbm",
        "JHOnlineData/mmBattleMstarWqvga.cbm",
        "JHOnlineData/mmTitleMstarWqvga.cbm",
    };
    for (u32 i = 0; i < sizeof(payloadPaths) / sizeof(payloadPaths[0]); ++i)
    {
        u32 len = vm_net_mock_load_response_file(payloadPaths[i], out, outCap);
        if (len == 0)
            continue;
        if (len < 1024)
        {
            vm_net_trace("mock_update_payload skip_too_small path=%s len=%u\n", payloadPaths[i], len);
            continue;
        }
        if (!vm_net_mock_buffer_has_nonzero(out, len))
        {
            vm_net_trace("mock_update_payload skip_zero path=%s len=%u\n", payloadPaths[i], len);
            continue;
        }
        vm_net_trace("mock_update_payload path=%s len=%u\n", payloadPaths[i], len);
        return len;
    }
    return 0;
}

static u32 vm_net_mock_actor_resource_index(u8 actorJob, u8 actorSex)
{
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    if (actorSex > 1)
        actorSex = 0;
    return (u32)(actorJob - 1u) * 2u + (u32)actorSex;
}

static bool vm_net_mock_put_bytes(u8 *out, u32 outCap, u32 *pos, const void *data, u32 len);
static bool vm_net_mock_put_u8(u8 *out, u32 outCap, u32 *pos, u8 value);
static u8 vm_net_mock_env_u8(const char *name, u8 fallback);
static const char *vm_net_mock_env_str(const char *name, const char *fallback);
static const char *vm_net_mock_actor_preview_image_name(u8 actorJob, u8 actorSex);

static bool vm_net_mock_title_login_requires_server_select(void)
{
    const char *spec = getenv("CBE_LOGIN_REQUIRE_SERVER_SELECT");
    if (spec != NULL && spec[0] == '0' && spec[1] == '\0')
        return false;
    return true;
}

static void vm_net_mock_title_login_phase_reset(const char *reason)
{
    if (g_netMockTitleServerListPending || g_netMockTitleServerSelectConfirmed || g_netMockTitleSelectedServerId)
    {
        vm_net_trace("mock_title_login_phase_reset reason=%s pending=%u confirmed=%u serverID=%u listTick=%u selectTick=%u\n",
                     reason ? reason : "?",
                     (u32)g_netMockTitleServerListPending,
                     (u32)g_netMockTitleServerSelectConfirmed,
                     g_netMockTitleSelectedServerId,
                     g_netMockTitleServerListTick,
                     g_netMockTitleServerSelectTick);
    }
    g_netMockTitleServerListPending = 0;
    g_netMockTitleServerSelectConfirmed = 0;
    g_netMockTitleServerListTick = 0;
    g_netMockTitleServerSelectTick = 0;
    g_netMockTitleSelectedServerId = 0;
}

static void vm_net_mock_title_login_phase_mark_server_list(void)
{
    g_netMockTitleServerListPending = 1;
    g_netMockTitleServerSelectConfirmed = 0;
    g_netMockTitleServerListTick = g_schedulerTick;
    g_netMockTitleServerSelectTick = 0;
    g_netMockTitleSelectedServerId = 0;
    vm_net_trace("mock_title_login_phase server_list pending=1 confirmed=0 tick=%u evidence=runtime:builtin_login_serverinfo_response_without_1_4_confirm\n",
                 g_schedulerTick);
}

static void vm_net_mock_title_login_phase_mark_server_select(u32 serverId)
{
    g_netMockTitleServerListPending = 1;
    g_netMockTitleServerSelectConfirmed = 1;
    g_netMockTitleServerSelectTick = g_schedulerTick;
    g_netMockTitleSelectedServerId = serverId;
    vm_net_trace("mock_title_login_phase server_select pending=1 confirmed=1 serverID=%u tick=%u listTick=%u evidence=runtime:client_sent_1_4_serverID_moneytype\n",
                 serverId,
                 g_schedulerTick,
                 g_netMockTitleServerListTick);
}

static const char *vm_net_mock_actor_ui_motion_name(u8 actorJob, u8 actorSex)
{
    static const char *uiActorNames[6] = {
        "ui_h_war.actor",
        "ui_hw_war.actor",
        "ui_h_ass.actor",
        "ui_hw_ass.actor",
        "ui_h_mag.actor",
        "ui_hw_mag.actor",
    };
    u32 tableIndex = vm_net_mock_actor_resource_index(actorJob, actorSex);
    if (tableIndex >= sizeof(uiActorNames) / sizeof(uiActorNames[0]))
        tableIndex = 0;
    return uiActorNames[tableIndex];
}

static bool vm_net_mock_put_le16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    if (*pos + 2 > outCap)
        return false;
    out[(*pos)++] = (u8)(value & 0xff);
    out[(*pos)++] = (u8)(value >> 8);
    return true;
}

static u32 vm_net_mock_build_minimal_actor_motion_resource(u8 *out, u32 outCap)
{
    /*
     * File layout expected by sub_100D48A: little-endian resource payload size,
     * followed by a resource stream.  Type 0xf1 is a mock-only raw DF stream.
     * sub_100D6E2 parses this stream as:
     * skipped dword, width/height, image-entry count, image name, frame bounds,
     * then three optional table counts.  Keep one real image entry so scene
     * rendering owns a valid image object while the unknown tables stay empty.
     */
    u8 actorJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u8 actorSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);
    const char *innerActor = vm_net_mock_env_str("CBE_ACTOR_MOTION_IMAGE_RESOURCE",
                                                 vm_net_mock_actor_ui_motion_name(actorJob, actorSex));
    u32 innerLen = (u32)strlen(innerActor);
    if (innerLen == 0 || innerLen > 0xff)
        return 0;

    u8 descriptor[128];
    u32 descPos = 0;
    memset(descriptor, 0, sizeof(descriptor));
    if (!vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 67) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 293) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 1) ||
        !vm_net_mock_put_u8(descriptor, sizeof(descriptor), &descPos, (u8)innerLen) ||
        !vm_net_mock_put_bytes(descriptor, sizeof(descriptor), &descPos, (const u8 *)innerActor, innerLen) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 67) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 293) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0))
        return 0;

    u32 payloadLen = 1 + descPos;
    u32 fileLen = 4 + payloadLen;
    if (outCap < fileLen)
        return 0;
    out[0] = (u8)payloadLen;
    out[1] = (u8)(payloadLen >> 8);
    out[2] = (u8)(payloadLen >> 16);
    out[3] = (u8)(payloadLen >> 24);
    out[4] = 0xf1;
    memcpy(out + 5, descriptor, descPos);
    vm_net_trace("mock_motion_wrapper job=%u sex=%u inner=%s len=%u\n", actorJob, actorSex, innerActor, fileLen);
    return fileLen;
}

static u32 vm_net_mock_build_minimal_actor_image_resource(u8 *out, u32 outCap)
{
    /*
     * sub_100D564 parses this stream as an image-sequence descriptor:
     * image resource names, cell width/height, sheet width/height, then a
     * cell-index table.  The actual bitmap remains the original game GIF.
     */
    static const u8 descriptor[] = {
        0x01, 0x00, 0x00, 0x00,                         /* image resource count */
        0x12, 'h', '_', 'w', 'a', 'r', 'r', 'i', 'o',
        'r', 'w', 'a', 'l', 'k', '2', '.', 'g', 'i', 'f',
        0x43, 0x00, 0x00, 0x00,                         /* cell width */
        0x25, 0x01, 0x00, 0x00,                         /* cell height */
        0x43, 0x00, 0x00, 0x00,                         /* sheet width */
        0x25, 0x01, 0x00, 0x00,                         /* sheet height */
        0x00, 0x00, 0x00, 0x00,                         /* single cell index */
    };
    u32 payloadLen = 1 + (u32)sizeof(descriptor);
    u32 fileLen = 4 + payloadLen;
    if (outCap < fileLen)
        return 0;
    out[0] = (u8)payloadLen;
    out[1] = (u8)(payloadLen >> 8);
    out[2] = (u8)(payloadLen >> 16);
    out[3] = (u8)(payloadLen >> 24);
    out[4] = 0xf1;
    memcpy(out + 5, descriptor, sizeof(descriptor));
    return fileLen;
}

static u32 vm_net_mock_build_len_prefixed_resource_blob(
    const u8 *payload, u32 payloadLen, u8 *out, u32 outCap)
{
    if (payload == NULL || payloadLen == 0 || out == NULL)
        return 0;
    if (payloadLen + 4 > outCap)
        return 0;

    out[0] = (u8)payloadLen;
    out[1] = (u8)(payloadLen >> 8);
    out[2] = (u8)(payloadLen >> 16);
    out[3] = (u8)(payloadLen >> 24);
    memcpy(out + 4, payload, payloadLen);
    return payloadLen + 4;
}

static bool vm_net_mock_string_has_non_ascii(const char *text)
{
    if (text == NULL)
        return false;
    while (*text)
    {
        if ((u8)*text >= 0x80)
            return true;
        ++text;
    }
    return false;
}

static bool vm_net_mock_named_payload_looks_like_resource_stream(
    const char *payloadName, u8 requestType, const u8 *payload, u32 payloadLen)
{
    u32 declaredLen = 0;

    if (payload == NULL || payloadLen == 0)
        return false;
    if (requestType != 1)
        return true;
    if (payloadName == NULL || payloadName[0] == 0)
        return true;
    if (strchr(payloadName, '.') != NULL)
        return true;
    if (!vm_net_mock_string_has_non_ascii(payloadName))
        return true;
    if (payloadLen < 4)
        return false;

    declaredLen = (u32)payload[0] |
                  ((u32)payload[1] << 8) |
                  ((u32)payload[2] << 16) |
                  ((u32)payload[3] << 24);
    return declaredLen > 0 && declaredLen <= payloadLen - 4;
}

static u32 vm_net_mock_load_named_update_payload(const char *payloadName, u8 *out, u32 outCap)
{
    if (payloadName == NULL || payloadName[0] == 0 || out == NULL || outCap == 0)
        return 0;

    char path[384];
    snprintf(path, sizeof(path), "JHOnlineData/%s", payloadName);
    return vm_net_mock_load_response_file(path, out, outCap);
}

static u32 vm_net_mock_load_resource_update_payload(
    const u8 *request, u32 requestLen, const char *requestedName, u8 *out, u32 outCap, const char **payloadName)
{
    u8 requestType = 0;
    bool haveRequestType = vm_net_mock_get_object_u8_field(request, requestLen, "type", &requestType);

    if (payloadName)
        *payloadName = (requestedName && requestedName[0]) ? requestedName : "MMORPGTempcbm";

    if (requestedName && requestedName[0])
    {
        if (strcmp(requestedName, "c00PenglaiXiandao_01") == 0)
            return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);

        if (strcmp(requestedName, "c_mock_missing_motion.actor") == 0 ||
            strcmp(requestedName, "mock_missing_motion.actor") == 0)
            return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);

        if (strcmp(requestedName, "mock_actor_image.gif") == 0)
            return vm_net_mock_build_minimal_actor_image_resource(out, outCap);

        u32 namedLen = vm_net_mock_load_named_update_payload(requestedName, out, outCap);
        if (namedLen > 0)
        {
            if (vm_net_mock_named_payload_looks_like_resource_stream(requestedName,
                                                                     haveRequestType ? requestType : 0,
                                                                     out,
                                                                     namedLen))
                return namedLen;
            vm_net_trace("mock_update_named_resource_reject_invalid_cache name=%s reqType=%u payloadLen=%u\n",
                         requestedName,
                         haveRequestType ? requestType : 0,
                         namedLen);
        }

        if ((!haveRequestType || requestType == 1) && outCap > 4)
        {
            u32 fallbackLen = vm_net_mock_load_update_payload(out + 4, outCap - 4);
            if (fallbackLen > 0)
            {
                u32 wrappedLen = vm_net_mock_build_len_prefixed_resource_blob(out + 4, fallbackLen, out, outCap);
                if (wrappedLen > 0)
                {
                    vm_net_trace("mock_update_named_resource_wrapper name=%s reqType=%u payloadLen=%u fileLen=%u\n",
                                 requestedName,
                                 haveRequestType ? requestType : 0,
                                 fallbackLen,
                                 wrappedLen);
                    return wrappedLen;
                }
            }
        }
    }

    if (vm_net_mock_request_contains(request, requestLen, "c_mock_missing_motion.actor") ||
        vm_net_mock_request_contains(request, requestLen, "mock_missing_motion.actor"))
    {
        if (payloadName)
            *payloadName = "c_mock_missing_motion.actor";
        return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);
    }

    if (vm_net_mock_request_contains(request, requestLen, "mock_actor_image.gif"))
    {
        if (payloadName)
            *payloadName = "mock_actor_image.gif";
        return vm_net_mock_build_minimal_actor_image_resource(out, outCap);
    }

    return vm_net_mock_load_update_payload(out, outCap);
}

static void vm_net_mock_save_tempdata(const u8 *data, u32 len)
{
    if (data == NULL || len == 0)
        return;
    FILE *fp = fopen("JHOnlineData/mmorpgTempdata", "wb");
    if (fp == NULL)
        return;
    fwrite(data, 1, len, fp);
    u8 payload[65536];
    u32 payloadLen = vm_net_mock_load_update_payload(payload, sizeof(payload));
    if (payloadLen > 0)
    {
        fwrite(payload, 1, payloadLen, fp);
        if (payloadLen < sizeof(payload))
        {
            static const u8 zeros[256] = {0};
            u32 remain = (u32)sizeof(payload) - payloadLen;
            while (remain > 0)
            {
                u32 chunk = remain > sizeof(zeros) ? (u32)sizeof(zeros) : remain;
                fwrite(zeros, 1, chunk, fp);
                remain -= chunk;
            }
        }
    }
    fclose(fp);
    vm_net_trace("mock_save_tempdata headerLen=%u payloadLen=%u\n", len, payloadLen);
}

static void vm_net_trace(const char *fmt, ...)
{
    if (!vm_net_trace_is_battle_g6_relevant(fmt))
        return;

    vm_trace_ensure_log_dir();

    FILE *fp = fopen("logs/net_trace.log", "ab");
    if (fp == NULL)
        return;
    vm_trace_write_net_session_marker(fp);

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fclose(fp);
}

static bool vm_net_trace_prefix_is(const char *text, const char *prefix)
{
    size_t prefixLen = 0;
    if (text == NULL || prefix == NULL)
        return false;
    prefixLen = strlen(prefix);
    return strncmp(text, prefix, prefixLen) == 0;
}

static bool vm_net_trace_battle_g6_only_enabled(void)
{
    const char *spec = getenv("CBE_BATTLE_G6_TRACE_ONLY");
    if (spec != NULL && spec[0] == '0' && spec[1] == '\0')
        return false;
    return true;
}

static bool vm_net_trace_is_battle_g6_relevant(const char *text)
{
    if (!vm_net_trace_battle_g6_only_enabled())
        return true;
    if (text == NULL)
        return false;

    return vm_net_trace_prefix_is(text, "mock_battle_operate_response") ||
           vm_net_trace_prefix_is(text, "mock_battle_operate_response_fallback") ||
           vm_net_trace_prefix_is(text, "mock_battle_operate_response_raw82") ||
           vm_net_trace_prefix_is(text, "mock_battle_operate_actioninfo_payload") ||
           vm_net_trace_prefix_is(text, "mock_battle_operate_actioninfo_payload_fallback") ||
           vm_net_trace_prefix_is(text, "trace_battle_actioninfo_materialize_detail") ||
           vm_net_trace_prefix_is(text, "trace_battle_actioninfo_materialize_scratch") ||
           vm_net_trace_prefix_is(text, "trace_battle_actioninfo_materialize_record") ||
           vm_net_trace_prefix_is(text, "trace_battle_actioninfo_materialize_effect_template") ||
           vm_net_trace_prefix_is(text, "trace_battle_state4_detail") ||
           vm_net_trace_prefix_is(text, "trace_battle_state4_local_record") ||
           vm_net_trace_prefix_is(text, "trace_battle_state4_current_block") ||
           vm_net_trace_prefix_is(text, "trace_battle_state4_active_action_block") ||
           vm_net_trace_prefix_is(text, "trace_battle_anim_effect_delta_detail") ||
           vm_net_trace_prefix_is(text, "trace_battle_apply_detail") ||
           vm_net_trace_prefix_is(text, "trace_battle_apply_state_window") ||
           vm_net_trace_prefix_is(text, "trace_battle_apply_stage_block") ||
           vm_net_trace_prefix_is(text, "trace_battle_apply_action_block") ||
           vm_net_trace_prefix_is(text, "trace_battle_module_state label=sub_4B70") ||
           vm_net_trace_prefix_is(text, "trace_battle_local_action_state label=sub_4B70") ||
           vm_net_trace_prefix_is(text, "trace_battle_local_action_state label=state4") ||
           vm_net_trace_prefix_is(text, "trace_battle_local_action_state label=type1_") ||
           vm_net_trace_prefix_is(text, "trace_title_") ||
           vm_net_trace_prefix_is(text, "trace_outgoing_wt_send_context") ||
           vm_net_trace_prefix_is(text, "mock_title_") ||
           vm_net_trace_prefix_is(text, "mock_login_") ||
           vm_net_trace_prefix_is(text, "mock_default source=builtin-login") ||
           vm_net_trace_prefix_is(text, "mock_default source=builtin-title") ||
           vm_net_trace_prefix_is(text, "handled_packet source=builtin-login") ||
           vm_net_trace_prefix_is(text, "handled_packet source=builtin-title") ||
           vm_net_trace_prefix_is(text, "send ") ||
           vm_net_trace_prefix_is(text, "mock_response") ||
           vm_net_trace_prefix_is(text, "queue_data_event") ||
           vm_net_trace_prefix_is(text, "fire_event") ||
           vm_net_trace_prefix_is(text, "touch_dispatch") ||
           vm_net_trace_prefix_is(text, "mouse_event") ||
           vm_net_trace_prefix_is(text, "unhandled_packet");
}

static void vm_net_write_bytes_dump(FILE *fp, const char *tag, const u8 *data, u32 len)
{
    if (fp == NULL)
        return;

    u32 dumpLen = len < 160 ? len : 160;
    fprintf(fp, "%s len=%u dump=%u hex=", tag, len, dumpLen);
    for (u32 i = 0; i < dumpLen; ++i)
        fprintf(fp, "%02x", data[i]);
    fprintf(fp, " ascii=");
    for (u32 i = 0; i < dumpLen; ++i)
    {
        u8 ch = data[i];
        fputc((ch >= 0x20 && ch <= 0x7e) ? ch : '.', fp);
    }
    fputc('\n', fp);
}

static void vm_net_trace_bytes(const char *tag, const u8 *data, u32 len)
{
    if (!vm_net_trace_is_battle_g6_relevant(tag))
        return;

    vm_trace_ensure_log_dir();

    FILE *fp = fopen("logs/net_trace.log", "ab");
    if (fp == NULL)
        return;
    vm_trace_write_net_session_marker(fp);
    vm_net_write_bytes_dump(fp, tag, data, len);
    fclose(fp);
}

static void vm_net_packet_trace_bytes(const char *tag, const u8 *data, u32 len)
{
    vm_trace_ensure_log_dir();

    FILE *fp = fopen("logs/net_packets.log", "ab");
    if (fp == NULL)
        return;
    vm_trace_write_net_session_marker(fp);
    vm_net_write_bytes_dump(fp, tag, data, len);
    fclose(fp);
}

static void vm_net_packet_trace(const char *fmt, ...)
{
    static u8 s_packet_session_started = 0;

    vm_trace_ensure_log_dir();

    FILE *fp = fopen("logs/net_packets.log", "ab");
    if (fp == NULL)
        return;
    if (!s_packet_session_started)
    {
        fprintf(fp, "\n==== session_start channel=net_packets ====\n");
        s_packet_session_started = 1;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fclose(fp);
}

static void vm_net_packet_summary_append(char *out, size_t outCap, size_t *used, const char *fmt, ...)
{
    if (out == NULL || outCap == 0 || used == NULL || *used >= outCap)
        return;

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(out + *used, outCap - *used, fmt, args);
    va_end(args);
    if (written < 0)
        return;
    size_t advance = (size_t)written;
    if (advance >= outCap - *used)
        *used = outCap - 1;
    else
        *used += advance;
}

static void vm_net_packet_build_request_summary(const u8 *request, u32 requestLen, char *out, size_t outCap)
{
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;

    if (request == NULL || requestLen == 0)
    {
        snprintf(out, outCap, "empty");
        return;
    }

    if (requestLen < 2 || request[0] != 'W' || request[1] != 'T')
    {
        snprintf(out, outCap, "raw len=%u", requestLen);
        return;
    }

    size_t used = 0;
    u8 headerKind = 0;
    u8 headerSubtype = 0;
    u32 offset = 4;
    u32 objectCount = 0;
    vm_net_mock_request_object object;

    (void)vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &headerKind, &headerSubtype);
    vm_net_packet_summary_append(out, outCap, &used, "WT len=%u hdr=%u/%u objs=",
                                 requestLen, headerKind, headerSubtype);

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        u8 typeValue = 0;
        u8 resultValue = 0;
        u32 idValue = 0;
        bool hasType = vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &typeValue);
        bool hasResult = vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "result", &resultValue);
        bool hasId = vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &idValue);

        if (objectCount != 0)
            vm_net_packet_summary_append(out, outCap, &used, ",");
        if (objectCount >= 8)
        {
            vm_net_packet_summary_append(out, outCap, &used, "...");
            break;
        }

        vm_net_packet_summary_append(out, outCap, &used, "%u/%u/%u",
                                     (u32)object.major, (u32)object.kind, (u32)object.subtype);
        if (hasType || hasResult || hasId)
        {
            vm_net_packet_summary_append(out, outCap, &used, "(");
            if (hasType)
                vm_net_packet_summary_append(out, outCap, &used, "type=%u", (u32)typeValue);
            if (hasResult)
                vm_net_packet_summary_append(out, outCap, &used, "%sresult=%u", hasType ? "," : "", (u32)resultValue);
            if (hasId)
                vm_net_packet_summary_append(out, outCap, &used, "%sid=%u", (hasType || hasResult) ? "," : "", idValue);
            vm_net_packet_summary_append(out, outCap, &used, ")");
        }
        ++objectCount;
    }

    if (objectCount == 0)
        vm_net_packet_summary_append(out, outCap, &used, "<none>");
    vm_net_packet_summary_append(out, outCap, &used, " count=%u", objectCount);
}

static void vm_net_log_handled_packet(const char *source, const u8 *request, u32 requestLen, u32 responseLen)
{
    char summary[512];
    vm_net_packet_build_request_summary(request, requestLen, summary, sizeof(summary));
    vm_net_trace("handled_packet source=%s responseLen=%u %s\n",
                 source ? source : "unknown",
                 responseLen,
                 summary);
    snprintf(g_netLastHandledSource, sizeof(g_netLastHandledSource), "%s", source ? source : "unknown");
    snprintf(g_netLastHandledSummary, sizeof(g_netLastHandledSummary), "%s", summary);
    g_netLastHandledResponseLen = responseLen;
    g_netLastHandledValid = 1;
}

static void vm_net_packet_log_exchange(const u8 *request, u32 requestLen, const u8 *response, u32 responseLen)
{
    if (!g_netLastHandledValid)
        return;

    vm_net_packet_trace("游戏请求数据包\n");
    vm_net_packet_trace_bytes("send_payload", request, requestLen);
    vm_net_packet_trace("主机处理信息 tick=%u source=%s responseLen=%u phase=%u/%u serverID=%u phaseTick=%u/%u last=%08x active=%08x current=%08x %s\n",
                        g_schedulerTick,
                        g_netLastHandledSource,
                        g_netLastHandledResponseLen,
                        (u32)g_netMockTitleServerListPending,
                        (u32)g_netMockTitleServerSelectConfirmed,
                        g_netMockTitleSelectedServerId,
                        g_netMockTitleServerListTick,
                        g_netMockTitleServerSelectTick,
                        lastAddress,
                        vmAddedScreen,
                        g_currentScreenThis,
                        g_netLastHandledSummary);
    vm_net_packet_trace("主机响应数据包\n");
    vm_net_packet_trace_bytes("mock_response_payload", response, responseLen);
}

static void vm_net_read_active_ui_name_best_effort(char *out, size_t outCap);

static void vm_net_trace_outgoing_wt_send_context(const u8 *request, u32 requestLen, u32 connectId, u32 dataPtr, u32 dataLen)
{
    static u32 s_outgoingWtSendContextCount = 0;
    u32 offset = 4;
    u32 objectCount = 0;
    u32 firstKind = 0;
    u32 firstSubtype = 0;
    u32 id = 0;
    u32 index = 0;
    u32 posx = 0;
    u32 posy = 0;
    u32 operate = 0;
    u32 pc = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 r6 = 0;
    u32 r7 = 0;
    u32 eventObj = 0;
    u32 eventCount = 0;
    u32 eventCap = 0;
    u32 eventBase = 0;
    u32 netObjCb = 0;
    u32 netObjFlush = 0;
    u8 eventByte5 = 0;
    char uiName[96];
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return;
    if (s_outgoingWtSendContextCount >= 80)
        return;

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (objectCount == 0)
        {
            firstKind = object.kind;
            firstSubtype = object.subtype;
        }
        ++objectCount;
    }
    if (!((firstKind == 4 && firstSubtype == 1) ||
          (firstKind == 4 && firstSubtype == 2) ||
          (firstKind == 2 && firstSubtype == 10) ||
          (firstKind == 1 && (firstSubtype == 1 ||
                              firstSubtype == 4 ||
                              firstSubtype == 6 ||
                              firstSubtype == 12 ||
                              firstSubtype == 16))))
    {
        return;
    }

    (void)vm_net_mock_get_object_u32_field(request, requestLen, "id", &id);
    if (!vm_net_mock_get_object_u32_field(request, requestLen, "index", &index))
    {
        u8 index8 = 0;
        if (vm_net_mock_get_object_u8_field(request, requestLen, "index", &index8))
            index = index8;
    }
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posx", &posx);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posy", &posy);
    if (!vm_net_mock_get_object_u32_field(request, requestLen, "Operate", &operate))
    {
        u8 operate8 = 0;
        if (vm_net_mock_get_object_u8_field(request, requestLen, "Operate", &operate8))
            operate = operate8;
    }

    uiName[0] = 0;
    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    vm_net_read_active_ui_name_best_effort(uiName, sizeof(uiName));

    if (Global_R9)
    {
        (void)uc_mem_read(MTK, Global_R9 + 0x5540, &eventObj, 4);
        if (eventObj)
        {
            (void)uc_mem_read(MTK, eventObj + 0x05, &eventByte5, 1);
            (void)uc_mem_read(MTK, eventObj + 0x10, &eventCount, 4);
            (void)uc_mem_read(MTK, eventObj + 0x14, &eventCap, 4);
            (void)uc_mem_read(MTK, eventObj + 0x18, &eventBase, 4);
        }
    }
    if (g_netCurrentObject)
    {
        (void)uc_mem_read(MTK, g_netCurrentObject + 0x14, &netObjCb, 4);
        (void)uc_mem_read(MTK, g_netCurrentObject + 0x2c, &netObjFlush, 4);
    }

    ++s_outgoingWtSendContextCount;
    vm_net_trace("trace_outgoing_wt_send_context first=%u/%u objectCount=%u connect=%u dataPtr=%08x dataLen=%u requestLen=%u"
                 " pc=%08x lr=%08x last=%08x sp=%08x tick=%u regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x"
                 " id=%u index=%u pos=%u,%u operate=%u netObj=%08x netCb=%08x netFlush=%08x"
                 " eventObj=%08x byte5=%u eventCount=%u eventCap=%u eventBase=%08x uiName=%s activeScreen=%08x currentThis=%08x count=%u"
                 " evidence=runtime:vm_net_mock_on_send_boundary\n",
                 firstKind,
                 firstSubtype,
                 objectCount,
                 connectId,
                 dataPtr,
                 dataLen,
                 requestLen,
                 pc,
                 lr,
                 lastAddress,
                 sp,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 id,
                 index,
                 posx,
                 posy,
                 operate,
                 g_netCurrentObject,
                 netObjCb,
                 netObjFlush,
                 eventObj,
                 eventByte5,
                 eventCount,
                 eventCap,
                 eventBase,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_outgoingWtSendContextCount);
}

static void vm_net_log_unhandled_packet(const u8 *request, u32 requestLen)
{
    char summary[512];
    vm_net_packet_build_request_summary(request, requestLen, summary, sizeof(summary));
    vm_net_trace("unhandled_packet %s\n", summary);
    vm_net_trace_bytes("unhandled_packet_payload", request, requestLen);
    vm_net_packet_trace("游戏请求数据包\n");
    vm_net_packet_trace_bytes("send_payload", request, requestLen);
    vm_net_packet_trace("主机处理信息 unhandled_packet %s\n", summary);
    vm_net_packet_trace("主机响应数据包\n");
    vm_net_packet_trace("assert(0)\n");
}

static bool vm_net_try_read_ascii(u32 ptr, u8 *out, u32 outCap)
{
    if (ptr == 0 || out == NULL || outCap == 0)
        return false;

    memset(out, 0, outCap);
    for (u32 i = 0; i + 1 < outCap; ++i)
    {
        u8 ch = 0;
        if (uc_mem_read(MTK, ptr + i, &ch, 1) != UC_ERR_OK)
            return false;
        if (ch == 0)
        {
            out[i] = 0;
            return i > 0;
        }
        if (ch < 0x20 || ch > 0x7e)
            return false;
        out[i] = ch;
    }
    out[outCap - 1] = 0;
    return false;
}

static void vm_net_read_guest_ascii_label(u32 ptr, char *out, size_t outCap)
{
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (ptr == 0)
    {
        snprintf(out, outCap, "<null>");
        return;
    }
    if (vm_net_try_read_ascii(ptr, (u8 *)out, (u32)outCap))
        return;
    snprintf(out, outCap, "<nonascii:%08x>", ptr);
}

static void vm_net_read_guest_gbk_label(u32 ptr, char *out, size_t outCap)
{
    u8 raw[64];
    size_t i = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (ptr == 0)
    {
        snprintf(out, outCap, "<null>");
        return;
    }
    memset(raw, 0, sizeof(raw));
    if (uc_mem_read(MTK, ptr, raw, sizeof(raw) - 1) != UC_ERR_OK)
    {
        snprintf(out, outCap, "<bad:%08x>", ptr);
        return;
    }
    while (i < sizeof(raw) - 1 && raw[i] != 0)
        i++;
    raw[i] = 0;
    if (i == 0)
    {
        snprintf(out, outCap, "<empty>");
        return;
    }
    gbk_to_utf8(raw, out, outCap);
}

static bool vm_net_read_guest_raw_cstr(u32 ptr, char *out, size_t outCap)
{
    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (ptr == 0)
        return false;

    for (size_t i = 0; i + 1 < outCap; ++i)
    {
        u8 ch = 0;
        if (uc_mem_read(MTK, ptr + (u32)i, &ch, 1) != UC_ERR_OK)
        {
            out[0] = 0;
            return false;
        }
        out[i] = (char)ch;
        if (ch == 0)
            return i > 0;
    }
    out[outCap - 1] = 0;
    return out[0] != 0;
}

static void vm_net_read_guest_best_effort_label(u32 ptr, char *out, size_t outCap)
{
    char asciiBuf[96];

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;

    vm_net_read_guest_ascii_label(ptr, asciiBuf, sizeof(asciiBuf));
    if (strncmp(asciiBuf, "<nonascii:", 10) == 0)
    {
        vm_net_read_guest_gbk_label(ptr, out, outCap);
        if (out[0] != 0)
            return;
    }
    snprintf(out, outCap, "%s", asciiBuf);
}

static void vm_net_read_active_ui_name(char *out, size_t outCap)
{
    u32 uiObj = 0;
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (!Global_R9)
    {
        snprintf(out, outCap, "<no-r9>");
        return;
    }
    if (uc_mem_read(MTK, Global_R9 + 21676, &uiObj, 4) != UC_ERR_OK || uiObj == 0)
    {
        snprintf(out, outCap, "<no-ui>");
        return;
    }
    vm_net_read_guest_ascii_label(uiObj + 1141, out, outCap);
}

static void vm_net_read_active_ui_name_best_effort(char *out, size_t outCap)
{
    u32 uiObj = 0;
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (!Global_R9)
    {
        snprintf(out, outCap, "<no-r9>");
        return;
    }
    if (uc_mem_read(MTK, Global_R9 + 21676, &uiObj, 4) != UC_ERR_OK || uiObj == 0)
    {
        snprintf(out, outCap, "<no-ui>");
        return;
    }
    vm_net_read_guest_best_effort_label(uiObj + 1141, out, outCap);
}

static void vm_net_trace_resource_name_flow(const char *tag, u32 pc, u32 lr, u32 namePtr)
{
    char nameBuf[96];
    char uiName[96];
    vm_net_read_guest_ascii_label(namePtr, nameBuf, sizeof(nameBuf));
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("%s pc=%08x lr=%08x last=%08x tick=%u namePtr=%08x name=%s uiName=%s\n",
                 tag, pc, lr, lastAddress, g_schedulerTick, namePtr, nameBuf, uiName);
}

static void vm_net_trace_resource_request_enqueue(const char *tag,
                                                  u32 pc,
                                                  u32 lr,
                                                  u32 requestType,
                                                  u32 namePtr,
                                                  u32 argA4,
                                                  u32 argA5,
                                                  u32 argA6,
                                                  u32 argA7,
                                                  u32 argA8)
{
    char nameBuf[96];
    char uiName[96];

    vm_net_read_guest_best_effort_label(namePtr, nameBuf, sizeof(nameBuf));
    vm_net_read_active_ui_name_best_effort(uiName, sizeof(uiName));
    vm_net_trace("%s pc=%08x lr=%08x last=%08x tick=%u reqType=%u namePtr=%08x name=%s a4=%08x a5=%08x a6=%08x a7=%08x a8=%08x uiName=%s activeScreen=%08x currentThis=%08x\n",
                 tag,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 requestType,
                 namePtr,
                 nameBuf,
                 argA4,
                 argA5,
                 argA6,
                 argA7,
                 argA8,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_update_request_prepare(u32 pc, u32 lr, u32 requestType)
{
    u32 pendingEntry = 0;
    u32 start = 0;
    u32 version = 0;
    u16 resourceType = 0;
    char nameBuf[96];
    char uiName[96];
    vm_net_read_guest_best_effort_label(0, nameBuf, sizeof(nameBuf));
    vm_net_read_active_ui_name_best_effort(uiName, sizeof(uiName));
    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 38284, &pendingEntry, 4);
        uc_mem_read(MTK, Global_R9 + 38328, &version, 4);
    }
    if (pendingEntry)
    {
        uc_mem_read(MTK, pendingEntry, &resourceType, 2);
        uc_mem_read(MTK, pendingEntry + 52, &start, 4);
        vm_net_read_guest_best_effort_label(pendingEntry + 2, nameBuf, sizeof(nameBuf));
    }
    vm_net_trace("trace_update_request_prepare pc=%08x lr=%08x last=%08x tick=%u reqType=%u pending=%08x name=%s type=%u start=%u version=%u uiName=%s activeScreen=%08x currentThis=%08x\n",
                 pc, lr, lastAddress, g_schedulerTick, requestType, pendingEntry, nameBuf, (u32)resourceType, start, version, uiName, vmAddedScreen, g_currentScreenThis);
}

static void vm_net_trace_parse_actor_motion_entry(u32 pc, u32 lr, u32 arg0, u32 arg4, u32 arg8, u32 argC)
{
    char arg0Name[96];
    char arg4Name[96];
    char uiName[96];
    vm_net_read_guest_ascii_label(arg0, arg0Name, sizeof(arg0Name));
    vm_net_read_guest_ascii_label(arg4, arg4Name, sizeof(arg4Name));
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("trace_parse_actor_motion_entry pc=%08x lr=%08x last=%08x tick=%u arg0=%08x name=%s arg4=%08x arg4Name=%s arg8=%08x argC=%08x uiName=%s\n",
                 pc, lr, lastAddress, g_schedulerTick, arg0, arg0Name, arg4, arg4Name, arg8, argC, uiName);
}

static void vm_net_trace_sub_10352ae_callsite(const char *tag, u32 pc, u32 lr, u32 r0, u32 r1, u32 r2, u32 stackArg0, u32 stackArg4, u32 stackArg8)
{
    char stack0Name[96];
    char uiName[96];
    vm_net_read_guest_ascii_label(stackArg0, stack0Name, sizeof(stack0Name));
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("%s pc=%08x lr=%08x last=%08x tick=%u r0=%08x r1=%08x r2=%08x stack0=%08x stack0Name=%s stack4=%08x stack8=%08x uiName=%s\n",
                 tag, pc, lr, lastAddress, g_schedulerTick, r0, r1, r2, stackArg0, stack0Name, stackArg4, stackArg8, uiName);
}

static void vm_net_trace_encounter_table_records(const char *label, u32 tableBase)
{
    static u32 s_encounterTableTraceCount = 0;
    u32 ids[4] = {0};
    u32 raw0[4] = {0};
    u32 raw20[4] = {0};
    u32 raw28[4] = {0};
    u32 raw36[4] = {0};
    u32 raw44[4] = {0};

    if (tableBase == 0 || s_encounterTableTraceCount >= 96)
        return;

    for (u32 i = 0; i < 4; ++i)
    {
        u32 rec = tableBase + i * 0x4Cu;
        (void)uc_mem_read(MTK, rec + 0x00u, &raw0[i], 4);
        (void)uc_mem_read(MTK, rec + 0x20u, &raw20[i], 4);
        (void)uc_mem_read(MTK, rec + 0x24u, &ids[i], 4);
        (void)uc_mem_read(MTK, rec + 0x28u, &raw28[i], 4);
        (void)uc_mem_read(MTK, rec + 0x36u, &raw36[i], 4);
        (void)uc_mem_read(MTK, rec + 0x44u, &raw44[i], 4);
    }

    ++s_encounterTableTraceCount;
    vm_net_trace("trace_encounter_table_records label=%s table=%08x"
                 " ids=%u,%u,%u,%u"
                 " raw0=%08x,%08x,%08x,%08x"
                 " raw20=%08x,%08x,%08x,%08x"
                 " raw28=%08x,%08x,%08x,%08x"
                 " raw36=%08x,%08x,%08x,%08x"
                 " raw44=%08x,%08x,%08x,%08x"
                 " activeScreen=%08x currentThis=%08x count=%u evidence=main:sub_1010228_scene_table,Battle.cbm:sub_66A4_enemy_lookup_record_plus_0x24\n",
                 label ? label : "unknown",
                 tableBase,
                 ids[0], ids[1], ids[2], ids[3],
                 raw0[0], raw0[1], raw0[2], raw0[3],
                 raw20[0], raw20[1], raw20[2], raw20[3],
                 raw28[0], raw28[1], raw28[2], raw28[3],
                 raw36[0], raw36[1], raw36[2], raw36[3],
                 raw44[0], raw44[1], raw44[2], raw44[3],
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_encounterTableTraceCount);
}

static void vm_net_trace_challenge_request_source(const char *label, u32 pc)
{
    static u32 s_challengeSourceTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 record = 0;
    u32 offset = 0;
    u32 base = 0;
    u32 index = 0;
    u32 raw00 = 0;
    u32 raw04 = 0;
    u32 raw08 = 0;
    u32 raw0c = 0;
    u32 raw10 = 0;
    u32 raw14 = 0;
    u32 raw20 = 0;
    u32 raw38 = 0;
    u32 raw44 = 0;
    u32 raw48 = 0;
    u32 raw54 = 0;
    u32 raw64 = 0;
    char recName[96];
    char uiName[96];

    if (s_challengeSourceTraceCount >= 160)
        return;

    recName[0] = 0;
    uiName[0] = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);

    /*
     * sub_1037ED4 source path:
     *   record = tableBase + selectedIndex * 0x74
     *   id     = *(record + 0x48)
     * At 0x01037ECC, R0 already holds record and R1 holds the byte offset.
     * At 0x01037ED4, R2 holds the id after the packet field name is loaded.
     */
    if (pc == 0x01037ECC || pc == 0x01037ECE)
    {
        record = r0;
        offset = r1;
    }
    else if (pc == 0x01037ED4)
    {
        /*
         * The record pointer is no longer in a stable register here, but the
         * just-written id is in R2. Keep the register dump to tie the outgoing
         * field write to the preceding 0x01037ECC record trace.
         */
        record = 0;
        offset = 0;
    }

    if (record != 0 && offset % 0x74u == 0)
    {
        index = offset / 0x74u;
        base = record - offset;
        (void)uc_mem_read(MTK, record + 0x00u, &raw00, 4);
        (void)uc_mem_read(MTK, record + 0x04u, &raw04, 4);
        (void)uc_mem_read(MTK, record + 0x08u, &raw08, 4);
        (void)uc_mem_read(MTK, record + 0x0cu, &raw0c, 4);
        (void)uc_mem_read(MTK, record + 0x10u, &raw10, 4);
        (void)uc_mem_read(MTK, record + 0x14u, &raw14, 4);
        (void)uc_mem_read(MTK, record + 0x20u, &raw20, 4);
        (void)uc_mem_read(MTK, record + 0x38u, &raw38, 4);
        (void)uc_mem_read(MTK, record + 0x44u, &raw44, 4);
        (void)uc_mem_read(MTK, record + 0x48u, &raw48, 4);
        (void)uc_mem_read(MTK, record + 0x54u, &raw54, 4);
        (void)uc_mem_read(MTK, record + 0x64u, &raw64, 4);
        vm_net_read_guest_best_effort_label(record + 0x0cu, recName, sizeof(recName));
    }
    vm_net_read_active_ui_name_best_effort(uiName, sizeof(uiName));

    ++s_challengeSourceTraceCount;
    vm_net_trace("trace_challenge_request_source label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x"
                 " table=%08x record=%08x selectedIndex=%u offset=%08x requestId=%u"
                 " raw00=%08x raw04=%08x raw08=%08x raw0c=%08x raw10=%08x raw14=%08x"
                 " raw20=%08x raw38=%08x raw44=%08x raw48=%08x raw54=%08x raw64=%08x"
                 " recName=%s uiName=%s activeScreen=%08x currentThis=%08x count=%u"
                 " evidence=IDA:sub_1037ED4_4_1_id_from_record_plus_0x48\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 base,
                 record,
                 index,
                 offset,
                 (pc == 0x01037ED4) ? r2 : raw48,
                 raw00,
                 raw04,
                 raw08,
                 raw0c,
                 raw10,
                 raw14,
                 raw20,
                 raw38,
                 raw44,
                 raw48,
                 raw54,
                 raw64,
                 recName,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_challengeSourceTraceCount);
}

static void vm_net_trace_outgoing_field_callsite(const char *label, u32 pc)
{
    static u32 s_outgoingFieldTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 r6 = 0;
    u32 r7 = 0;
    u32 eventObj = 0;
    u32 eventCount = 0;
    u32 eventCap = 0;
    u32 eventBase = 0;
    u8 eventByte5 = 0;
    char fieldName[64];
    char uiName[96];

    if (s_outgoingFieldTraceCount >= 420)
        return;

    fieldName[0] = 0;
    uiName[0] = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);

    vm_net_read_guest_ascii_label(r1, fieldName, sizeof(fieldName));
    vm_net_read_active_ui_name_best_effort(uiName, sizeof(uiName));
    if (Global_R9)
    {
        (void)uc_mem_read(MTK, Global_R9 + 0x5540, &eventObj, 4);
        if (eventObj)
        {
            (void)uc_mem_read(MTK, eventObj + 0x05, &eventByte5, 1);
            (void)uc_mem_read(MTK, eventObj + 0x10, &eventCount, 4);
            (void)uc_mem_read(MTK, eventObj + 0x14, &eventCap, 4);
            (void)uc_mem_read(MTK, eventObj + 0x18, &eventBase, 4);
        }
    }

    ++s_outgoingFieldTraceCount;
    vm_net_trace("trace_outgoing_field_callsite label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " packetObj=%08x fieldPtr=%08x field=%s value=%u valueHex=%08x writerCb=%08x"
                 " regs4=%08x,%08x,%08x,%08x eventObj=%08x byte5=%u count=%u cap=%u base=%08x"
                 " uiName=%s activeScreen=%08x currentThis=%08x countTrace=%u"
                 " evidence=IDA:sub_1037C9C_or_sub_1037F6E_packet_field_writer_callsite\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 fieldName,
                 r2,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 eventObj,
                 eventByte5,
                 eventCount,
                 eventCap,
                 eventBase,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_outgoingFieldTraceCount);
}

static void vm_net_trace_sub_1010228_entry(u32 pc, u32 lr)
{
    u32 srcObj = 0;
    u32 dstObj = 0;
    u32 field68 = 0;
    u8 flag5c74 = 0;
    u8 flag5c79 = 0;
    char copiedName[96];
    char uiName[96];

    copiedName[0] = 0;
    uiName[0] = 0;
    if (!Global_R9)
        return;

    uc_mem_read(MTK, Global_R9 + 23716, &srcObj, 4);
    uc_mem_read(MTK, Global_R9 + 23796, &dstObj, 4);
    uc_mem_read(MTK, Global_R9 + 23668, &flag5c74, 1);
    uc_mem_read(MTK, Global_R9 + 23673, &flag5c79, 1);
    if (srcObj)
    {
        uc_mem_read(MTK, srcObj + 36, &field68, 4);
        vm_net_read_guest_ascii_label(srcObj + 68, copiedName, sizeof(copiedName));
    }
    else
    {
        vm_net_read_guest_ascii_label(0, copiedName, sizeof(copiedName));
    }
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("trace_sub_1010228_entry pc=%08x lr=%08x last=%08x tick=%u srcObj=%08x dstObj=%08x copiedName=%s field36=%08x flagsBefore=%u,%u uiName=%s activeScreen=%08x currentThis=%08x\n",
                 pc, lr, lastAddress, g_schedulerTick, srcObj, dstObj, copiedName, field68, flag5c74, flag5c79, uiName, vmAddedScreen, g_currentScreenThis);
    vm_net_trace_encounter_table_records("sub_1010228_entry_src", srcObj);
    vm_net_trace_encounter_table_records("sub_1010228_entry_dst", dstObj);
}

static void vm_net_trace_sub_1010228_callsite(const char *label, u32 pc)
{
    u32 srcObj = 0;
    u32 dstObj = 0;
    u32 field36 = 0;
    u32 lr = 0;
    u8 flag5c74 = 0;
    u8 flag5c79 = 0;
    char copiedName[96];
    char uiName[96];

    copiedName[0] = 0;
    uiName[0] = 0;
    if (!Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, Global_R9 + 23716, &srcObj, 4);
    uc_mem_read(MTK, Global_R9 + 23796, &dstObj, 4);
    uc_mem_read(MTK, Global_R9 + 23668, &flag5c74, 1);
    uc_mem_read(MTK, Global_R9 + 23673, &flag5c79, 1);
    if (srcObj)
    {
        uc_mem_read(MTK, srcObj + 36, &field36, 4);
        vm_net_read_guest_ascii_label(srcObj + 68, copiedName, sizeof(copiedName));
    }
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("trace_sub_1010228_callsite label=%s pc=%08x lr=%08x last=%08x tick=%u srcObj=%08x dstObj=%08x copiedName=%s field36=%08x flagsBefore=%u,%u uiName=%s activeScreen=%08x currentThis=%08x\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 srcObj,
                 dstObj,
                 copiedName,
                 field36,
                 flag5c74,
                 flag5c79,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis);
    vm_net_trace_encounter_table_records("sub_1010228_callsite_src", srcObj);
    vm_net_trace_encounter_table_records("sub_1010228_callsite_dst", dstObj);
}

static void vm_net_trace_add_role_to_list(const char *label, u32 pc)
{
    static u32 s_addRoleTraceCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 r6 = 0;
    u32 r7 = 0;
    u32 stackArgs[6] = {0};
    u32 compactObj = 0;
    u8 groupCount = 0;
    char nameArg[96];

    if (s_addRoleTraceCount >= 96)
        return;

    nameArg[0] = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    if (sp)
        (void)uc_mem_read(MTK, sp, stackArgs, sizeof(stackArgs));
    vm_net_read_guest_ascii_label(r1, nameArg, sizeof(nameArg));
    if (Global_R9)
    {
        (void)uc_mem_read(MTK, Global_R9 + 23796, &compactObj, 4);
        (void)uc_mem_read(MTK, Global_R9 + 23668, &groupCount, 1);
    }

    ++s_addRoleTraceCount;
    vm_net_trace("trace_add_role_to_list label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " args=%u,%08x,%u,%u stack=%u,%u,%u,%u,%u,%u regs4=%08x,%08x,%08x,%08x"
                 " nameArg=%s compactObj=%08x groupCount=%u count=%u"
                 " evidence=IDA:main_CBE_AddRoleToList_0x01011E1E_writes_R9_plus_23796\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 stackArgs[0],
                 stackArgs[1],
                 stackArgs[2],
                 stackArgs[3],
                 stackArgs[4],
                 stackArgs[5],
                 r4,
                 r5,
                 r6,
                 r7,
                 nameArg,
                 compactObj,
                 groupCount,
                 s_addRoleTraceCount);
    vm_net_trace_encounter_table_records("add_role_to_list_compact", compactObj);
}

static void vm_net_trace_groupinfo_case5_reader(const char *label, u32 pc)
{
    static u32 s_groupinfoCase5TraceCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 regs[8] = {0};
    u32 reader = 0;
    u32 state = 0;
    u32 cursor = 0;
    u32 blobObj = 0;
    u32 blobData = 0;
    u32 blobLen = 0;
    u32 cb1c = 0;
    u32 cb20 = 0;
    u32 cb24 = 0;
    u32 cb28 = 0;
    u32 cb2c = 0;
    u32 locals[10] = {0};
    u8 nextBytes[32];
    char nextHex[128];
    char r0Label[64];
    char r1Label[64];

    if (s_groupinfoCase5TraceCount >= 160)
        return;

    memset(nextBytes, 0, sizeof(nextBytes));
    nextHex[0] = 0;
    r0Label[0] = 0;
    r1Label[0] = 0;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &regs[0]);
    uc_reg_read(MTK, UC_ARM_REG_R1, &regs[1]);
    uc_reg_read(MTK, UC_ARM_REG_R2, &regs[2]);
    uc_reg_read(MTK, UC_ARM_REG_R3, &regs[3]);
    uc_reg_read(MTK, UC_ARM_REG_R4, &regs[4]);
    uc_reg_read(MTK, UC_ARM_REG_R5, &regs[5]);
    uc_reg_read(MTK, UC_ARM_REG_R6, &regs[6]);
    uc_reg_read(MTK, UC_ARM_REG_R7, &regs[7]);

    reader = sp + 0x40;
    state = reader + 0x400;
    if (sp)
    {
        (void)uc_mem_read(MTK, sp + 0x24, &locals[0], sizeof(locals));
    }
    if (state)
    {
        (void)uc_mem_read(MTK, state + 0x00, &cursor, 4);
        (void)uc_mem_read(MTK, state + 0x08, &blobObj, 4);
        (void)uc_mem_read(MTK, state + 0x1c, &cb1c, 4);
        (void)uc_mem_read(MTK, state + 0x20, &cb20, 4);
        (void)uc_mem_read(MTK, state + 0x24, &cb24, 4);
        (void)uc_mem_read(MTK, state + 0x28, &cb28, 4);
        (void)uc_mem_read(MTK, state + 0x2c, &cb2c, 4);
    }
    if (blobObj)
    {
        (void)uc_mem_read(MTK, blobObj + 0x04, &blobData, 4);
        (void)uc_mem_read(MTK, blobObj + 0x08, &blobLen, 4);
    }
    if (blobData && cursor < blobLen)
    {
        u32 avail = blobLen - cursor;
        u32 readLen = avail < sizeof(nextBytes) ? avail : (u32)sizeof(nextBytes);
        if (uc_mem_read(MTK, blobData + cursor, nextBytes, readLen) == UC_ERR_OK)
            vm_format_trace_bytes_hex(nextBytes, readLen, nextHex, sizeof(nextHex));
    }
    if (nextHex[0] == 0)
        snprintf(nextHex, sizeof(nextHex), "unreadable");
    vm_net_read_guest_ascii_label(regs[0], r0Label, sizeof(r0Label));
    vm_net_read_guest_ascii_label(regs[1], r1Label, sizeof(r1Label));

    ++s_groupinfoCase5TraceCount;
    vm_net_trace("trace_groupinfo_case5_reader label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " reader=%08x state=%08x cursor=%u blobObj=%08x blobData=%08x blobLen=%u next=%s"
                 " cb1c=%08x cb20=%08x cb24=%08x cb28=%08x cb2c=%08x"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x r0Text=%s r1Text=%s"
                 " locals_sp24=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x count=%u"
                 " evidence=IDA:main_CBE_net_handle_group_info_subtype5_0x0101208C_reader_order_20_2c_1c_28_28_20x4_to_AddRoleToList_0x01012116\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 reader,
                 state,
                 cursor,
                 blobObj,
                 blobData,
                 blobLen,
                 nextHex,
                 cb1c,
                 cb20,
                 cb24,
                 cb28,
                 cb2c,
                 regs[0],
                 regs[1],
                 regs[2],
                 regs[3],
                 regs[4],
                 regs[5],
                 regs[6],
                 regs[7],
                 r0Label,
                 r1Label,
                 locals[0],
                 locals[1],
                 locals[2],
                 locals[3],
                 locals[4],
                 locals[5],
                 locals[6],
                 locals[7],
                 locals[8],
                 locals[9],
                 s_groupinfoCase5TraceCount);
}

static void vm_net_trace_title_screen_callback(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 targetW0 = 0;
    u32 targetW1 = 0;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    if (r1)
    {
        (void)uc_mem_read(MTK, r1, &targetW0, 4);
        (void)uc_mem_read(MTK, r1 + 4, &targetW1, 4);
    }

    vm_net_trace("trace_title_screen_callback label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " routerObj=%08x target=%08x arg2=%08x method=%08x targetW0=%08x targetW1=%08x"
                 " activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 targetW0,
                 targetW1,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_title_login_state(const char *label)
{
    u32 base = g_currentScreenModuleBase;
    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);
    if (!base)
    {
        vm_net_trace("trace_title_login_state label=%s base=00000000 activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                     label ? label : "?",
                     vmAddedScreen,
                     g_currentScreenThis,
                     g_schedulerTick,
                     lastAddress);
        return;
    }

    u8 state0 = 0;
    u8 focus = 0;
    u8 saveDefault = 0;
    u16 mode = 0;
    u16 sel0 = 0;
    u16 sel1 = 0;
    u32 modeObjPtr = 0;
    u32 modeObjCb0 = 0;
    u32 modeObjCb4 = 0;
    u32 modeObjCb8 = 0;
    u32 modeObjCb10 = 0;
    u32 modeObjCb14 = 0;
    u32 choiceObj1 = 0;
    u32 choiceObj2 = 0;
    u32 target48 = 0;
    u32 target4c = 0;
    u32 target50 = 0;
    u32 target54 = 0;
    u32 choiceObj1Cb14 = 0;
    u32 choiceObj2Cb14 = 0;
    u32 target48Cb14 = 0;
    u32 target4cCb14 = 0;
    u32 target50Cb14 = 0;
    u32 target54Cb14 = 0;
    u32 listPtr = 0;
    u8 listCount = 0;
    u32 serverListPtr = 0;
    u8 serverListCount = 0;
    u8 serverListResult = 0;
    u8 serverListNewVer = 0;
    u32 serverListColor0 = 0;
    char serverListName0[96];
    char serverListStatus0[96];
    u32 roleFamily = 0;
    u32 screenMgr = 0;
    u8 stageFlag = 0;
    u8 altPromptGate = 0;
    u8 altPromptConfirm = 0;

    (void)uc_mem_read(MTK, base + 10728, &state0, 1);
    (void)uc_mem_read(MTK, base + 10730, &focus, 1);
    (void)uc_mem_read(MTK, base + 10735, &saveDefault, 1);
    (void)uc_mem_read(MTK, base + 10748, &mode, 2);
    (void)uc_mem_read(MTK, base + 10750, &sel0, 2);
    (void)uc_mem_read(MTK, base + 10752, &sel1, 2);
    (void)uc_mem_read(MTK, base + 10444, &modeObjPtr, 4);
    (void)uc_mem_read(MTK, base + 10792, &choiceObj1, 4);
    (void)uc_mem_read(MTK, base + 10796, &choiceObj2, 4);
    (void)uc_mem_read(MTK, base + 10800, &target48, 4);
    (void)uc_mem_read(MTK, base + 10804, &target4c, 4);
    (void)uc_mem_read(MTK, base + 10808, &target50, 4);
    (void)uc_mem_read(MTK, base + 10812, &target54, 4);
    (void)uc_mem_read(MTK, base + 10788, &listPtr, 4);
    (void)uc_mem_read(MTK, base + 10780, &serverListPtr, 4);
    (void)uc_mem_read(MTK, base + 11108, &roleFamily, 4);
    (void)uc_mem_read(MTK, base + 8276, &screenMgr, 4);
    (void)uc_mem_read(MTK, base + 10668, &altPromptGate, 1);
    (void)uc_mem_read(MTK, base + 10669, &altPromptConfirm, 1);
    if (screenMgr)
        (void)uc_mem_read(MTK, screenMgr + 357, &stageFlag, 1);
    if (modeObjPtr)
    {
        (void)uc_mem_read(MTK, modeObjPtr + 0x00, &modeObjCb0, 4);
        (void)uc_mem_read(MTK, modeObjPtr + 0x04, &modeObjCb4, 4);
        (void)uc_mem_read(MTK, modeObjPtr + 0x08, &modeObjCb8, 4);
        (void)uc_mem_read(MTK, modeObjPtr + 0x10, &modeObjCb10, 4);
        (void)uc_mem_read(MTK, modeObjPtr + 0x14, &modeObjCb14, 4);
    }
    if (choiceObj1)
        (void)uc_mem_read(MTK, choiceObj1 + 0x14, &choiceObj1Cb14, 4);
    if (choiceObj2)
        (void)uc_mem_read(MTK, choiceObj2 + 0x14, &choiceObj2Cb14, 4);
    if (target48)
        (void)uc_mem_read(MTK, target48 + 0x14, &target48Cb14, 4);
    if (target4c)
        (void)uc_mem_read(MTK, target4c + 0x14, &target4cCb14, 4);
    if (target50)
        (void)uc_mem_read(MTK, target50 + 0x14, &target50Cb14, 4);
    if (target54)
        (void)uc_mem_read(MTK, target54 + 0x14, &target54Cb14, 4);
    if (listPtr)
        (void)uc_mem_read(MTK, listPtr, &listCount, 1);
    memset(serverListName0, 0, sizeof(serverListName0));
    memset(serverListStatus0, 0, sizeof(serverListStatus0));
    if (serverListPtr)
    {
        (void)uc_mem_read(MTK, serverListPtr, &serverListCount, 1);
        (void)uc_mem_read(MTK, serverListPtr + 1, &serverListResult, 1);
        (void)uc_mem_read(MTK, serverListPtr + 2, &serverListNewVer, 1);
        if (serverListCount > 0)
        {
            vm_net_read_guest_gbk_label(serverListPtr + 3, serverListName0, sizeof(serverListName0));
            vm_net_read_guest_gbk_label(serverListPtr + 303, serverListStatus0, sizeof(serverListStatus0));
            (void)uc_mem_read(MTK, serverListPtr + 448, &serverListColor0, 4);
        }
    }

    vm_net_trace("trace_title_login_state label=%s base=%08x state0=%u focus=%u saveDefault=%u mode=%u sel=%u,%u"
                 " modeObj=%08x cb0=%08x cb4=%08x cb8=%08x cb10=%08x cb14=%08x"
                 " choice1=%08x cb14=%08x choice2=%08x cb14=%08x"
                 " target48=%08x target4c=%08x target50=%08x target54=%08x"
                 " targetCb14=%08x,%08x,%08x,%08x"
                 " listPtr=%08x listCount=%u roleFamily=%u stageFlag=%u altPrompt=%u,%u"
                 " activeScreen=%08x currentThis=%08x tick=%u last=%08x"
                 " serverListPtr=%08x serverCount=%u serverResult=%u serverNewVer=%u"
                 " server0.name=%s server0.status=%s server0.color=%08x\n",
                 label ? label : "?",
                 base,
                 state0,
                 focus,
                 saveDefault,
                 (u32)mode,
                 (u32)sel0,
                 (u32)sel1,
                 modeObjPtr,
                 modeObjCb0,
                 modeObjCb4,
                 modeObjCb8,
                 modeObjCb10,
                 modeObjCb14,
                 choiceObj1,
                 choiceObj1Cb14,
                 choiceObj2,
                 choiceObj2Cb14,
                 target48,
                 target4c,
                 target50,
                 target54,
                 target48Cb14,
                 target4cCb14,
                 target50Cb14,
                 target54Cb14,
                 listPtr,
                 (u32)listCount,
                 roleFamily,
                 (u32)stageFlag,
                 (u32)altPromptGate,
                 (u32)altPromptConfirm,
                 vmAddedScreen,
                 g_currentScreenThis,
                 g_schedulerTick,
                 lastAddress,
                 serverListPtr,
                 (u32)serverListCount,
                 (u32)serverListResult,
                 (u32)serverListNewVer,
                 serverListName0[0] ? serverListName0 : "<empty>",
                 serverListStatus0[0] ? serverListStatus0 : "<empty>",
                 serverListColor0);
}

static void vm_net_trace_title_login_site(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r9 = 0;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    vm_net_trace("trace_title_login_site label=%s pc=%08x lr=%08x regs=%08x,%08x,%08x,%08x r9=%08x"
                 " input=key(%d,%d) touch(down=%d up=%d drag=%d xy=%d,%d) tick=%u last=%08x"
                 " evidence=IDA:mmTitle_net_build_login_request_0x1B9C_n5_stage,login_form_submit_0x1E5C,server_select_0x2F62,login_action_0x553C\n",
                 label ? label : "?",
                 pc,
                 lr,
                 r0,
                 r1,
                 r2,
                 r3,
                 r9,
                 simulateKey,
                 simulatePress,
                 simulateTouchDown,
                 simulateTouchUp,
                 simulateTouchDrag,
                 simulateTouchX,
                 simulateTouchY,
                 g_schedulerTick,
                 lastAddress);
    vm_net_trace_title_login_state(label);
}

static void vm_net_trace_scene_owner_site(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 ownerObj = 0;
    u32 ownerState = 0;
    u32 ownerCb14 = 0;
    u32 ownerCb18 = 0;
    u32 ownerCb1c = 0;
    u32 ownerCb20 = 0;
    u32 ownerCb24 = 0;
    u32 ownerCb28 = 0;
    u32 ownerCb2c = 0;
    u32 ownerCb30 = 0;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (Global_R9)
    {
        (void)uc_mem_read(MTK, Global_R9 + 38056, &ownerObj, 4);
        if (ownerObj)
        {
            (void)uc_mem_read(MTK, ownerObj + 12, &ownerState, 4);
            (void)uc_mem_read(MTK, ownerObj + 20, &ownerCb14, 4);
            (void)uc_mem_read(MTK, ownerObj + 24, &ownerCb18, 4);
            (void)uc_mem_read(MTK, ownerObj + 28, &ownerCb1c, 4);
            (void)uc_mem_read(MTK, ownerObj + 32, &ownerCb20, 4);
            (void)uc_mem_read(MTK, ownerObj + 36, &ownerCb24, 4);
            (void)uc_mem_read(MTK, ownerObj + 40, &ownerCb28, 4);
            (void)uc_mem_read(MTK, ownerObj + 44, &ownerCb2c, 4);
            (void)uc_mem_read(MTK, ownerObj + 48, &ownerCb30, 4);
        }
    }

    vm_net_trace("trace_scene_owner_site label=%s pc=%08x lr=%08x last=%08x tick=%u ownerSlot=%08x ownerObj=%08x ownerState=%u"
                 " cb14=%08x cb18=%08x cb1c=%08x cb20=%08x cb24=%08x cb28=%08x cb2c=%08x cb30=%08x"
                 " activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 Global_R9 ? (Global_R9 + 38056) : 0,
                 ownerObj,
                 ownerState,
                 ownerCb14,
                 ownerCb18,
                 ownerCb1c,
                 ownerCb20,
                 ownerCb24,
                 ownerCb28,
                 ownerCb2c,
                 ownerCb30,
                 vmAddedScreen,
                 g_currentScreenThis);
}

void vm_net_trace_title_login_write(uint64_t address, uint32_t size, int64_t value)
{
    typedef struct
    {
        u32 offset;
        u32 width;
        const char *label;
    } title_field_watch;

    static const title_field_watch watches[] = {
        {10728, 1, "state0"},
        {10730, 1, "focus"},
        {10735, 1, "saveDefault"},
        {10737, 1, "slotLimit"},
        {10748, 2, "mode"},
        {10750, 2, "sel0"},
        {10752, 2, "sel1"},
        {10754, 1, "initDone"},
        {10756, 2, "ui56"},
        {10758, 2, "ui58"},
        {10760, 2, "ui60"},
        {10762, 2, "ui62"},
        {10764, 2, "ui64"},
        {10788, 4, "listPtr"},
        {10800, 4, "target48"},
        {10804, 4, "target4c"},
        {10808, 4, "target50"},
        {10812, 4, "target54"},
        {11046, 1, "roleReady"},
        {11108, 4, "roleFamily"},
        {11124, 4, "activeRoleIndex"},
    };

    u32 base = g_currentScreenModuleBase;
    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);
    if (!base || size == 0)
        return;

    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    for (u32 i = 0; i < mySizeOf(watches); ++i)
    {
        u32 fieldAddr = base + watches[i].offset;
        u32 fieldEnd = fieldAddr + watches[i].width;
        if (writeEnd <= fieldAddr || writeStart >= fieldEnd)
            continue;

        u32 oldValue = 0;
        u32 lowBits = watches[i].width * 8;
        u32 fieldMask = lowBits >= 32 ? 0xFFFFFFFFu : ((1u << lowBits) - 1u);
        u32 newValue = (u32)value & fieldMask;
        (void)uc_mem_read(MTK, fieldAddr, &oldValue, watches[i].width);

        vm_net_trace("trace_title_login_write label=%s addr=%08x size=%u old=%08x new=%08x"
                     " writeAddr=%08x writeSize=%u raw=%08x activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                     watches[i].label,
                     fieldAddr,
                     watches[i].width,
                     oldValue & fieldMask,
                     newValue,
                     writeStart,
                     size,
                     (u32)value,
                     vmAddedScreen,
                     g_currentScreenThis,
                     g_schedulerTick,
                     lastAddress);
    }
}

void vm_net_trace_title_role_workspace_write(uint64_t address, uint32_t size, int64_t value)
{
    typedef struct
    {
        u32 offset;
        u32 width;
        const char *label;
    } workspace_watch;

    static const workspace_watch watches[] = {
        {0x00, 1, "roleCount"},
        {0x68, 4, "role0.id"},
        {0xB0, 4, "role0.level"},
        {0x144, 1, "role0.sex"},
        {0x145, 1, "role0.job"},
        {0x7FC, 4, "selectedRoleIdShadow"},
    };

    u32 base = g_currentScreenModuleBase;
    u32 listPtr = 0;
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    char roleName[96];

    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);
    if (!base || size == 0)
        return;

    (void)uc_mem_read(MTK, base + 10788, &listPtr, 4);
    if (!listPtr)
        return;

    memset(roleName, 0, sizeof(roleName));

    for (u32 i = 0; i < mySizeOf(watches); ++i)
    {
        u32 fieldAddr = listPtr + watches[i].offset;
        u32 fieldEnd = fieldAddr + watches[i].width;
        if (writeEnd <= fieldAddr || writeStart >= fieldEnd)
            continue;

        u32 oldValue = 0;
        u32 lowBits = watches[i].width * 8;
        u32 fieldMask = lowBits >= 32 ? 0xFFFFFFFFu : ((1u << lowBits) - 1u);
        u32 newValue = (u32)value & fieldMask;
        (void)uc_mem_read(MTK, fieldAddr, &oldValue, watches[i].width);
        vm_net_read_guest_gbk_label(listPtr + 0x48, roleName, sizeof(roleName));

        vm_net_trace("trace_title_role_workspace_write label=%s addr=%08x size=%u old=%08x new=%08x"
                     " writeAddr=%08x writeSize=%u raw=%08x listPtr=%08x roleName=%s"
                     " activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                     watches[i].label,
                     fieldAddr,
                     watches[i].width,
                     oldValue & fieldMask,
                     newValue,
                     writeStart,
                     size,
                     (u32)value,
                     listPtr,
                     roleName[0] ? roleName : "<none>",
                     vmAddedScreen,
                     g_currentScreenThis,
                     g_schedulerTick,
                     lastAddress);
    }

    if (!(writeEnd <= listPtr + 0x48 || writeStart >= listPtr + 0x68))
    {
        vm_net_read_guest_gbk_label(listPtr + 0x48, roleName, sizeof(roleName));
        vm_net_trace("trace_title_role_workspace_write label=role0.name addr=%08x size=%u"
                     " writeAddr=%08x writeSize=%u raw=%08x listPtr=%08x roleName=%s"
                     " activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                     listPtr + 0x48,
                     0x20u,
                     writeStart,
                     size,
                     (u32)value,
                     listPtr,
                     roleName[0] ? roleName : "<none>",
                     vmAddedScreen,
                     g_currentScreenThis,
                     g_schedulerTick,
                     lastAddress);
    }
}

void vm_net_trace_shared_event_owner_write(uint64_t address, uint32_t size, int64_t value)
{
    u32 ownerSlot = 0;
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    u32 oldValue = 0;
    u32 newValue = 0;

    if (!Global_R9 || size == 0)
        return;

    ownerSlot = Global_R9 + 38056;
    if (writeEnd <= ownerSlot || writeStart >= ownerSlot + 4)
        return;

    (void)uc_mem_read(MTK, ownerSlot, &oldValue, 4);
    newValue = (u32)value;
    vm_net_trace("trace_shared_event_owner_write slot=%08x old=%08x new=%08x"
                 " writeAddr=%08x writeSize=%u raw=%08x activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                 ownerSlot,
                 oldValue,
                 newValue,
                 writeStart,
                 size,
                 (u32)value,
                 vmAddedScreen,
                 g_currentScreenThis,
                 g_schedulerTick,
                 lastAddress);
}

static void vm_net_trace_title_child_manager_call(const char *label, u32 pc)
{
    u32 r0 = 0;
    u32 r1 = 0;
    u32 lr = 0;
    u32 childPtr = 0;
    u32 cb4 = 0;
    u32 cb8 = 0;
    u32 cbC = 0;
    u32 cb10 = 0;
    u32 kind = 0;
    u32 subtype = 0;

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (r0)
    {
        (void)uc_mem_read(MTK, r0, &childPtr, 4);
    }
    if (childPtr)
    {
        (void)uc_mem_read(MTK, childPtr + 0x04, &cb4, 4);
        (void)uc_mem_read(MTK, childPtr + 0x08, &cb8, 4);
        (void)uc_mem_read(MTK, childPtr + 0x0C, &cbC, 4);
        (void)uc_mem_read(MTK, childPtr + 0x10, &cb10, 4);
    }
    if (r1)
    {
        (void)uc_mem_read(MTK, r1 + 4, &kind, 4);
        (void)uc_mem_read(MTK, r1 + 8, &subtype, 4);
    }

    vm_net_trace("trace_title_child_manager_call label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " mgr=%08x arg=%08x child=%08x cb4=%08x cb8=%08x cbC=%08x cb10=%08x"
                 " packetKind=%u packetSubtype=%u activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 childPtr,
                 cb4,
                 cb8,
                 cbC,
                 cb10,
                 kind,
                 subtype,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_business_dispatch_state(const char *label, u32 pc)
{
    static u32 s_businessDispatchTraceCount = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 lr = 0;
    u32 sceneObj = 0;
    u8 dispatchGate = 0;
    u32 objectCount = 0;
    u32 objectBase = 0;
    u32 fallbackCb = 0;
    u32 manager = 0;
    u32 managerChild = 0;
    u32 managerChildCb10 = 0;

    if (s_businessDispatchTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, 4);
        if (sceneObj)
            uc_mem_read(MTK, sceneObj + 0x164, &dispatchGate, 1);
        uc_mem_read(MTK, Global_R9 + 0x5590, &objectCount, 4);
        uc_mem_read(MTK, Global_R9 + 0x5598, &objectBase, 4);
        uc_mem_read(MTK, Global_R9 + 0x5D30, &fallbackCb, 4);
        manager = Global_R9 + 0x5EF0;
        uc_mem_read(MTK, manager, &managerChild, 4);
        if (managerChild)
            uc_mem_read(MTK, managerChild + 0x10, &managerChildCb10, 4);
    }

    ++s_businessDispatchTraceCount;
    vm_net_trace("trace_business_dispatch_state label=%s pc=%08x lr=%08x last=%08x tick=%u r0=%08x r1=%08x r2=%08x r3=%08x sceneObj=%08x dispatchGate=%u objectCount=%u objectBase=%08x fallbackCb=%08x manager=%08x managerChild=%08x managerChildCb10=%08x activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 sceneObj,
                 dispatchGate,
                 objectCount,
                 objectBase,
                 fallbackCb,
                 manager,
                 managerChild,
                 managerChildCb10,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_businessDispatchTraceCount);
}

static void vm_net_trace_shared_event_container(const char *label, u32 containerPtr)
{
    static u32 s_sharedEventContainerTraceCount = 0;
    u32 totalLen = 0;
    u32 objectCount = 0;
    u32 objectBase = 0;
    u32 capacity = 0;
    u32 entryCount = 0;
    u32 limit = 0;

    if (s_sharedEventContainerTraceCount >= 96 || containerPtr == 0)
        return;

    (void)uc_mem_read(MTK, containerPtr + 0x08, &totalLen, 4);
    (void)uc_mem_read(MTK, containerPtr + 0x0C, &objectCount, 4);
    (void)uc_mem_read(MTK, containerPtr + 0x10, &entryCount, 4);
    (void)uc_mem_read(MTK, containerPtr + 0x14, &capacity, 4);
    (void)uc_mem_read(MTK, containerPtr + 0x18, &objectBase, 4);

    ++s_sharedEventContainerTraceCount;
    vm_net_trace("trace_shared_event_container label=%s ptr=%08x totalLen=%u objectCount=%u entryCount=%u capacity=%u base=%08x tick=%u last=%08x count=%u\n",
                 label ? label : "?",
                 containerPtr,
                 totalLen,
                 objectCount,
                 entryCount,
                 capacity,
                 objectBase,
                 g_schedulerTick,
                 lastAddress,
                 s_sharedEventContainerTraceCount);

    if (objectBase == 0 || entryCount == 0)
        return;

    limit = entryCount < 4 ? entryCount : 4;
    for (u32 i = 0; i < limit; ++i)
    {
        u32 entryPtr = objectBase + i * 0x58u;
        u32 major = 0;
        u32 kind = 0;
        u32 subtype = 0;
        u32 payloadTag = 0;
        u32 fieldCount = 0;
        u32 fieldBase = 0;
        (void)uc_mem_read(MTK, entryPtr + 0x00, &major, 4);
        (void)uc_mem_read(MTK, entryPtr + 0x04, &kind, 4);
        (void)uc_mem_read(MTK, entryPtr + 0x08, &subtype, 4);
        (void)uc_mem_read(MTK, entryPtr + 0x0C, &payloadTag, 4);
        (void)uc_mem_read(MTK, entryPtr + 0x10, &fieldCount, 4);
        (void)uc_mem_read(MTK, entryPtr + 0x18, &fieldBase, 4);
        vm_net_trace("trace_shared_event_container_entry label=%s index=%u entry=%08x major=%u kind=%u subtype=%u payloadTag=%u fieldCount=%u fieldBase=%08x tick=%u\n",
                     label ? label : "?",
                     i,
                     entryPtr,
                     major,
                     kind,
                     subtype,
                     payloadTag,
                     fieldCount,
                     fieldBase,
                     g_schedulerTick);
    }
}

static void vm_net_trace_battle_sub17ac_loop_gate(u32 tracePc, u32 traceOff)
{
    static u32 s_battleSub17acLoopGateTraceCount = 0;
    u32 sp = 0;
    u32 r3 = 0;
    u32 loopCtx = 0;
    u32 entryCount = 0;
    u32 loopIndex = 0;
    u32 entryBase = 0;

    if (s_battleSub17acLoopGateTraceCount >= 64)
        return;

    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    if (r3 != 0)
        (void)vm_net_trace_read_u32(r3 + 0x40u, &loopCtx);
    if (loopCtx != 0)
    {
        (void)vm_net_trace_read_u32(loopCtx + 0x10u, &entryCount);
        (void)vm_net_trace_read_u32(loopCtx + 0x18u, &entryBase);
    }
    if (sp != 0)
        (void)vm_net_trace_read_u32(sp + 0x538u, &loopIndex);

    ++s_battleSub17acLoopGateTraceCount;
    vm_net_trace("trace_battle_sub17ac_loop_gate pc=%08x off=%04x tick=%u sp=%08x r3=%08x loopCtx=%08x entryCount=%u loopIndex=%u entryBase=%08x last=%08x count=%u evidence=Battle.cbm:sub_17AC_common_loop_count_gate\n",
                 tracePc,
                 traceOff,
                 g_schedulerTick,
                 sp,
                 r3,
                 loopCtx,
                 entryCount,
                 loopIndex,
                 entryBase,
                 lastAddress,
                 s_battleSub17acLoopGateTraceCount);
}

static void vm_net_trace_business_dispatch_item(u32 pc)
{
    static u32 s_businessDispatchItemTraceCount = 0;
    u32 entry = 0;
    u32 index = 0;
    u32 kind = 0;
    u32 subtype = 0;
    u32 lr = 0;

    if (s_businessDispatchItemTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R4, &entry);
    uc_reg_read(MTK, UC_ARM_REG_R5, &index);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (entry)
    {
        uc_mem_read(MTK, entry + 4, &kind, 4);
        uc_mem_read(MTK, entry + 8, &subtype, 4);
    }

    ++s_businessDispatchItemTraceCount;
    vm_net_trace("trace_business_dispatch_item pc=%08x lr=%08x last=%08x tick=%u index=%u entry=%08x kind=%u subtype=%u activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 index,
                 entry,
                 kind,
                 subtype,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_businessDispatchItemTraceCount);
}

static void vm_net_trace_practise_info_parser(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 packet = 0;
    u32 kind = 0;
    u32 subtype = 0;
    u8 gate5530 = 0;
    u8 gate5531 = 0;
    u16 todayHour = 0;
    u16 todayMin = 0;
    u32 getExp = 0;
    u16 allHour = 0;
    u16 allMin = 0;
    u8 isGold = 0;

    if (!Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &packet);
    if (packet)
    {
        uc_mem_read(MTK, packet + 4, &kind, 4);
        uc_mem_read(MTK, packet + 8, &subtype, 4);
    }
    uc_mem_read(MTK, Global_R9 + 0x5530, &gate5530, 1);
    uc_mem_read(MTK, Global_R9 + 0x5531, &gate5531, 1);
    uc_mem_read(MTK, Global_R9 + 0x7A28 + 0x10, &todayHour, 2);
    uc_mem_read(MTK, Global_R9 + 0x7A28 + 0x12, &todayMin, 2);
    uc_mem_read(MTK, Global_R9 + 0x7A28 + 0x14, &getExp, 4);
    uc_mem_read(MTK, Global_R9 + 0x7A28 + 0x1C, &allHour, 2);
    uc_mem_read(MTK, Global_R9 + 0x7A28 + 0x1E, &allMin, 2);
    uc_mem_read(MTK, Global_R9 + 0x7A28 + 3, &isGold, 1);

    vm_net_trace("trace_practise_info_parser label=%s pc=%08x lr=%08x last=%08x tick=%u packet=%08x kind=%u subtype=%u gate5530=%u gate5531=%u today=%u:%u getexp=%u all=%u:%u isgold=%u activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 packet,
                 kind,
                 subtype,
                 gate5530,
                 gate5531,
                 todayHour,
                 todayMin,
                 getExp,
                 allHour,
                 allMin,
                 isGold,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_tasktypes_case13_stream(const char *label, u32 pc)
{
    static u32 s_tasktypesCase13TraceCount = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 r6 = 0;
    u32 r7 = 0;
    u32 lr = 0;
    u32 streamCursor = 0;
    u32 blobObj = 0;
    u32 blobPtr = 0;
    u32 blobLen = 0;
    u8 bytes[16] = {0};
    char hex[64] = {0};

    if (s_tasktypesCase13TraceCount >= 64)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (r5)
    {
        uc_mem_read(MTK, r5 + 0x400, &streamCursor, 4);
        uc_mem_read(MTK, r5 + 0x408, &blobObj, 4);
    }
    if (blobObj)
    {
        uc_mem_read(MTK, blobObj + 4, &blobPtr, 4);
        uc_mem_read(MTK, blobObj + 8, &blobLen, 4);
    }
    if (blobPtr)
        uc_mem_read(MTK, blobPtr + streamCursor, bytes, sizeof(bytes));
    vm_format_trace_bytes_hex(bytes, sizeof(bytes), hex, sizeof(hex));

    ++s_tasktypesCase13TraceCount;
    vm_net_trace("trace_tasktypes_case13_stream label=%s pc=%08x lr=%08x last=%08x tick=%u r0=%08x r1=%08x r2=%08x r4=%u r5=%08x r6=%08x r7=%08x cursor=%u blobObj=%08x blobPtr=%08x blobLen=%u next=%s activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r4,
                 r5,
                 r6,
                 r7,
                 streamCursor,
                 blobObj,
                 blobPtr,
                 blobLen,
                 hex,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_tasktypesCase13TraceCount);
}

static void vm_net_trace_title_login_dispatch(const char *label, u32 pc)
{
    u32 base = g_currentScreenModuleBase;
    u32 r0 = 0;
    u32 lr = 0;
    u32 kind = 0;
    u32 subtype = 0;
    u32 loginStatePtr = 0;
    u8 resultByte = 0;
    u32 target48 = 0;
    u32 target4c = 0;
    u32 target50 = 0;

    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (r0)
    {
        (void)uc_mem_read(MTK, r0 + 4, &kind, 4);
        (void)uc_mem_read(MTK, r0 + 8, &subtype, 4);
    }
    if (base)
    {
        (void)uc_mem_read(MTK, base + 10780, &loginStatePtr, 4);
        (void)uc_mem_read(MTK, base + 10800, &target48, 4);
        (void)uc_mem_read(MTK, base + 10804, &target4c, 4);
        (void)uc_mem_read(MTK, base + 10808, &target50, 4);
    }
    if (loginStatePtr)
        (void)uc_mem_read(MTK, loginStatePtr + 1, &resultByte, 1);

    vm_net_trace("trace_title_login_dispatch label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " packet=%08x kind=%u subtype=%u loginState=%08x resultByte=%u"
                 " target48=%08x target4c=%08x target50=%08x activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 kind,
                 subtype,
                 loginStatePtr,
                 (u32)resultByte,
                 target48,
                 target4c,
                 target50,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_title_role_path(const char *label, u32 pc)
{
    u32 base = g_currentScreenModuleBase;
    u32 lr = 0;
    u32 r0 = 0;
    u32 modeObj = 0;
    u32 modeObjCb4 = 0;
    u32 modeObjCb8 = 0;
    u32 modeObjCb10 = 0;
    u32 selectedRoleIdShadow = 0;
    u32 firstRoleId = 0;
    u32 firstRoleLevel = 0;
    u8 firstRoleSex = 0;
    u8 firstRoleJob = 0;
    char firstRoleName[96];
    u32 listPtr = 0;
    u32 activeRoleIndex = 0;
    u32 roleFamily = 0;
    u8 state0 = 0;
    u8 ready = 0;
    u8 slotLimit = 0;
    u8 initDone = 0;
    u8 roleCount = 0;
    u8 tag1 = 0;
    u8 tag2 = 0;
    u16 mode = 0;
    u16 sel0 = 0;
    u16 sel1 = 0;
    u16 ui56 = 0;
    u16 ui58 = 0;
    u16 ui60 = 0;
    u16 ui62 = 0;
    u16 ui64 = 0;

    memset(firstRoleName, 0, sizeof(firstRoleName));

    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (base)
    {
        (void)uc_mem_read(MTK, base + 10728, &state0, 1);
        (void)uc_mem_read(MTK, base + 10737, &slotLimit, 1);
        (void)uc_mem_read(MTK, base + 10748, &mode, 2);
        (void)uc_mem_read(MTK, base + 10750, &sel0, 2);
        (void)uc_mem_read(MTK, base + 10752, &sel1, 2);
        (void)uc_mem_read(MTK, base + 10754, &initDone, 1);
        (void)uc_mem_read(MTK, base + 10756, &ui56, 2);
        (void)uc_mem_read(MTK, base + 10758, &ui58, 2);
        (void)uc_mem_read(MTK, base + 10760, &ui60, 2);
        (void)uc_mem_read(MTK, base + 10762, &ui62, 2);
        (void)uc_mem_read(MTK, base + 10764, &ui64, 2);
        (void)uc_mem_read(MTK, base + 10788, &listPtr, 4);
        (void)uc_mem_read(MTK, base + 11046, &ready, 1);
        (void)uc_mem_read(MTK, base + 11108, &roleFamily, 4);
        (void)uc_mem_read(MTK, base + 11124, &activeRoleIndex, 4);
        (void)uc_mem_read(MTK, base + 10444, &modeObj, 4);
    }
    if (modeObj)
    {
        (void)uc_mem_read(MTK, modeObj + 0x04, &modeObjCb4, 4);
        (void)uc_mem_read(MTK, modeObj + 0x08, &modeObjCb8, 4);
        (void)uc_mem_read(MTK, modeObj + 0x10, &modeObjCb10, 4);
    }
    if (listPtr)
    {
        (void)uc_mem_read(MTK, listPtr, &roleCount, 1);
        (void)uc_mem_read(MTK, listPtr + 1, &tag1, 1);
        (void)uc_mem_read(MTK, listPtr + 2, &tag2, 1);
        (void)uc_mem_read(MTK, listPtr + 2044, &selectedRoleIdShadow, 4);
        if (roleCount > 0)
        {
            (void)uc_mem_read(MTK, listPtr + 104, &firstRoleId, 4);
            (void)uc_mem_read(MTK, listPtr + 176, &firstRoleLevel, 4);
            (void)uc_mem_read(MTK, listPtr + 324, &firstRoleSex, 1);
            (void)uc_mem_read(MTK, listPtr + 325, &firstRoleJob, 1);
            vm_net_read_guest_gbk_label(listPtr + 72, firstRoleName, sizeof(firstRoleName));
        }
    }

    vm_net_trace("trace_title_role_path label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " r0=%08x state0=%u ready=%u mode=%u sel=%u,%u"
                 " slotLimit=%u initDone=%u ui56=%u ui58=%u ui60=%u ui62=%u ui64=%u"
                 " listPtr=%08x roleCount=%u tag=%u,%u activeRoleIndex=%u roleFamily=%u"
                 " role0={id:%u lvl:%u sex:%u job:%u name:%s} selectedRoleIdShadow=%u"
                 " modeObj=%08x cb4=%08x cb8=%08x cb10=%08x activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 (u32)state0,
                 (u32)ready,
                 (u32)mode,
                 (u32)sel0,
                 (u32)sel1,
                 (u32)slotLimit,
                 (u32)initDone,
                 (u32)ui56,
                 (u32)ui58,
                 (u32)ui60,
                 (u32)ui62,
                 (u32)ui64,
                 listPtr,
                 (u32)roleCount,
                 (u32)tag1,
                 (u32)tag2,
                 activeRoleIndex,
                 roleFamily,
                 firstRoleId,
                 firstRoleLevel,
                 (u32)firstRoleSex,
                 (u32)firstRoleJob,
                 firstRoleName[0] ? firstRoleName : "<none>",
                 selectedRoleIdShadow,
                 modeObj,
                 modeObjCb4,
                 modeObjCb8,
                 modeObjCb10,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_title_rolelist_reader_methods(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 sp = 0;
    u32 readerBuf = 0;
    u32 fnV20 = 0;
    u32 fnV21 = 0;
    u32 fnV22 = 0;
    u32 fnV23 = 0;
    u32 fnV24 = 0;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);

    readerBuf = sp + 0x08;
    (void)uc_mem_read(MTK, sp + 0x424, &fnV20, 4);
    (void)uc_mem_read(MTK, sp + 0x428, &fnV21, 4);
    (void)uc_mem_read(MTK, sp + 0x42C, &fnV22, 4);
    (void)uc_mem_read(MTK, sp + 0x430, &fnV23, 4);
    (void)uc_mem_read(MTK, sp + 0x434, &fnV24, 4);

    vm_net_trace("trace_title_rolelist_reader_methods label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " sp=%08x readerBuf=%08x v20=%08x v21=%08x v22=%08x v23=%08x v24=%08x"
                 " activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 sp,
                 readerBuf,
                 fnV20,
                 fnV21,
                 fnV22,
                 fnV23,
                 fnV24,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_title_role_select_action(const char *label, u32 pc)
{
    u32 base = g_currentScreenModuleBase;
    u32 lr = 0;
    u32 r0 = 0;
    u32 listPtr = 0;
    u32 selectedRoleIdShadow = 0;
    u32 firstRoleId = 0;
    u32 roleFamily = 0;
    u32 activeRoleIndex = 0;
    u8 roleCount = 0;
    u16 mode = 0;
    u16 sel0 = 0;
    u16 sel1 = 0;
    char firstRoleName[96];

    memset(firstRoleName, 0, sizeof(firstRoleName));

    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);

    if (base)
    {
        (void)uc_mem_read(MTK, base + 10748, &mode, 2);
        (void)uc_mem_read(MTK, base + 10750, &sel0, 2);
        (void)uc_mem_read(MTK, base + 10752, &sel1, 2);
        (void)uc_mem_read(MTK, base + 10788, &listPtr, 4);
        (void)uc_mem_read(MTK, base + 11108, &roleFamily, 4);
        (void)uc_mem_read(MTK, base + 11124, &activeRoleIndex, 4);
    }
    if (listPtr)
    {
        (void)uc_mem_read(MTK, listPtr, &roleCount, 1);
        (void)uc_mem_read(MTK, listPtr + 2044, &selectedRoleIdShadow, 4);
        if (roleCount > 0)
        {
            (void)uc_mem_read(MTK, listPtr + 104, &firstRoleId, 4);
            vm_net_read_guest_gbk_label(listPtr + 72, firstRoleName, sizeof(firstRoleName));
        }
    }

    vm_net_trace("trace_title_role_select_action label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " r0=%08x mode=%u sel=%u,%u roleFamily=%u activeRoleIndex=%u"
                 " listPtr=%08x roleCount=%u selectedRoleIdShadow=%u role0.id=%u role0.name=%s"
                 " activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 (u32)mode,
                 (u32)sel0,
                 (u32)sel1,
                 roleFamily,
                 activeRoleIndex,
                 listPtr,
                 (u32)roleCount,
                 selectedRoleIdShadow,
                 firstRoleId,
                 firstRoleName[0] ? firstRoleName : "<none>",
                 vmAddedScreen,
                 g_currentScreenThis);
}

void vm_net_trace_current_net_object_write(uint64_t address, uint32_t size, int64_t value)
{
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    u32 callbackSlot = 0;
    u32 oldValue = 0;
    u32 newValue = 0;
    u32 callback0c = 0;
    u32 callback10 = 0;

    if (!g_netCurrentObject || size == 0)
        return;

    callbackSlot = g_netCurrentObject + 0x14;
    if (writeEnd <= callbackSlot || writeStart >= callbackSlot + 4)
        return;

    (void)uc_mem_read(MTK, callbackSlot - 8, &callback0c, 4);
    (void)uc_mem_read(MTK, callbackSlot - 4, &callback10, 4);
    (void)uc_mem_read(MTK, callbackSlot, &oldValue, 4);
    newValue = (u32)value;

    vm_net_trace("trace_current_net_object_write label=callback obj=%08x slot=%08x old=%08x new=%08x"
                 " peer0c=%08x peer10=%08x writeAddr=%08x writeSize=%u raw=%08x"
                 " activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                 g_netCurrentObject,
                 callbackSlot,
                 oldValue,
                 newValue,
                 callback0c,
                 callback10,
                 writeStart,
                 size,
                 (u32)value,
                 vmAddedScreen,
                 g_currentScreenThis,
                 g_schedulerTick,
                 lastAddress);
}

void vm_net_trace_scene_dispatch_gate_write(uint64_t address, uint32_t size, int64_t value)
{
    static u32 s_sceneDispatchGateWriteTraceCount = 0;
    u32 sceneObj = 0;
    u32 gateSlot = 0;
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    u8 oldValue = 0;
    u8 newValue = 0;
    u32 pc = 0;
    u32 lr = 0;
    u32 byteOffset = 0;

    if (!Global_R9 || size == 0 || s_sceneDispatchGateWriteTraceCount >= 64)
        return;
    if (uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, 4) != UC_ERR_OK || sceneObj == 0)
        return;

    gateSlot = sceneObj + 0x164;
    if (writeEnd <= gateSlot || writeStart > gateSlot)
        return;

    byteOffset = gateSlot - writeStart;
    if (byteOffset >= 8)
        return;

    (void)uc_mem_read(MTK, gateSlot, &oldValue, 1);
    newValue = (u8)(((uint64_t)value >> (byteOffset * 8u)) & 0xffu);
    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    ++s_sceneDispatchGateWriteTraceCount;
    vm_net_trace("trace_scene_dispatch_gate_write sceneObj=%08x slot=%08x old=%u new=%u writeAddr=%08x writeSize=%u raw=%08x pc=%08x lr=%08x last=%08x tick=%u activeScreen=%08x currentThis=%08x count=%u\n",
                 sceneObj,
                 gateSlot,
                 oldValue,
                 newValue,
                 writeStart,
                 size,
                 (u32)value,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneDispatchGateWriteTraceCount);
}

void vm_net_trace_scene_loading_owner_write(uint64_t address, uint32_t size, int64_t value)
{
    static u32 s_sceneLoadingOwnerWriteTraceCount = 0;
    static const struct
    {
        u32 offset;
        u32 width;
        const char *name;
    } targets[] = {
        {0x552C, 4, "loadingState_R9_552C"},
        {0x5530, 1, "loadingGate_R9_5530"},
        {0x5531, 1, "loadingGateBlock_R9_5531"},
        {0x5540, 4, "loadingWaitObj_R9_5540"},
        {0x554C, 4, "loadingWaitCallback_R9_554C"},
        {0x5564, 4, "loadingWaitObjFlag_R9_5564"},
        {0x5C64, 1, "sceneTickFlag0_R9_5C64"},
        {0x5C65, 1, "sceneTickOneShot_R9_5C65"},
        {0x5C66, 1, "sceneTickResync_R9_5C66"},
        {0x5C67, 1, "sceneGateA_R9_5C67"},
        {0x5C68, 1, "sceneGateB_R9_5C68"},
        {0x9590, 2, "netManagerCount_R9_9590"},
    };
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    u32 pc = 0;
    u32 lr = 0;

    if (!Global_R9 || size == 0 || s_sceneLoadingOwnerWriteTraceCount >= 256)
        return;

    for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
    {
        u32 slot = Global_R9 + targets[i].offset;
        u32 slotEnd = slot + targets[i].width;
        u32 byteOffset;
        u32 oldValue = 0;
        u32 newValue = 0;

        if (writeEnd <= slot || writeStart >= slotEnd || writeStart > slot)
            continue;

        byteOffset = slot - writeStart;
        if (byteOffset >= 8)
            continue;
        if (targets[i].width == 1)
        {
            u8 oldByte = 0;
            (void)uc_mem_read(MTK, slot, &oldByte, 1);
            oldValue = oldByte;
            newValue = (u8)(((uint64_t)value >> (byteOffset * 8u)) & 0xffu);
        }
        else if (targets[i].width == 2)
        {
            u16 oldWord = 0;
            (void)uc_mem_read(MTK, slot, &oldWord, 2);
            oldValue = oldWord;
            if (byteOffset <= 6)
                newValue = (u16)(((uint64_t)value >> (byteOffset * 8u)) & 0xffffu);
        }
        else
        {
            u32 oldDword = 0;
            (void)uc_mem_read(MTK, slot, &oldDword, 4);
            oldValue = oldDword;
            if (byteOffset <= 4)
                newValue = (u32)(((uint64_t)value >> (byteOffset * 8u)) & 0xffffffffu);
        }

        uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        ++s_sceneLoadingOwnerWriteTraceCount;
        vm_net_trace("trace_scene_loading_owner_write field=%s slot=%08x old=%u new=%u writeAddr=%08x writeSize=%u raw=%08x pc=%08x lr=%08x last=%08x tick=%u activeScreen=%08x currentThis=%08x count=%u\n",
                     targets[i].name,
                     slot,
                     oldValue,
                     newValue,
                     writeStart,
                     size,
                     (u32)value,
                     pc,
                     lr,
                     lastAddress,
                     g_schedulerTick,
                     vmAddedScreen,
                     g_currentScreenThis,
                     s_sceneLoadingOwnerWriteTraceCount);
    }
}

void vm_net_trace_battle_module_data_write(uint64_t address, uint32_t size, int64_t value)
{
    static u32 s_battleDataWriteTraceCount = 0;
    static const struct
    {
        u32 offset;
        u32 width;
        const char *name;
    } targets[] = {
        {0x2018, 4, "bridge_R9_2018"},
        {0x2040, 4, "draw_R9_2040"},
        {0x204C, 4, "streamMgr_R9_204C"},
        {0x2050, 4, "mainObj_R9_2050"},
        {0x3450, 0x80, "battleState_R9_3450"},
        {0x374C, 0x190, "fighterTables_R9_374C"},
        {0x4058, 1, "pendingNet_R9_4058"},
    };
    u32 battleR9 = 0;
    u32 battleDataEnd = 0;
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    u32 pc = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    const char *label = NULL;
    u32 watchedSlot = 0;
    u32 oldValue = 0;
    u32 newValue = 0;

    if (size == 0 || s_battleDataWriteTraceCount >= 512)
        return;
    if (writeEnd < writeStart)
        return;
    {
        u32 loaderR9 = vm_screen_stack_lookup_module_base(vmAddedScreen);
        if (loaderR9 == 0)
            loaderR9 = g_currentScreenModuleBase;
        if (loaderR9 != 0 && writeStart < loaderR9 + 0x4600u && writeEnd > loaderR9)
        {
            battleR9 = loaderR9;
            battleDataEnd = loaderR9 + 0x4600u;
        }
    }
    if (battleR9 == 0 && writeStart < 0x05098600u && writeEnd > 0x05094000u)
    {
        battleR9 = 0x05094000u;
        battleDataEnd = 0x05098600u;
    }
    else if (battleR9 == 0 && writeStart < 0x050AC600u && writeEnd > 0x050A8000u)
    {
        battleR9 = 0x050A8000u;
        battleDataEnd = 0x050AC600u;
    }
    else if (battleR9 == 0)
    {
        u32 inferredCodeBase = 0;
        u32 inferredR9 = 0;
        if (vm_infer_battle_module_from_screen(vmAddedScreen, &inferredCodeBase, &inferredR9) &&
            writeStart < inferredR9 + 0x4600u && writeEnd > inferredR9)
        {
            battleR9 = inferredR9;
            battleDataEnd = inferredR9 + 0x4600u;
        }
    }

    if (battleR9 == 0)
        return;

    for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
    {
        u32 slot = battleR9 + targets[i].offset;
        u32 slotEnd = slot + targets[i].width;
        if (writeEnd <= slot || writeStart >= slotEnd)
            continue;
        label = targets[i].name;
        watchedSlot = slot;
        if (targets[i].width <= 4 && writeStart <= slot)
        {
            u32 byteOffset = slot - writeStart;
            if (byteOffset < 8)
            {
                (void)uc_mem_read(MTK, slot, &oldValue, targets[i].width);
                newValue = (u32)(((uint64_t)value >> (byteOffset * 8u)) &
                                  (targets[i].width == 1 ? 0xffu :
                                   targets[i].width == 2 ? 0xffffu : 0xffffffffu));
            }
        }
        break;
    }

    if (!label)
        return;

    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);

    ++s_battleDataWriteTraceCount;
    vm_net_trace("trace_battle_module_data_write label=%s dataBase=%08x slot=%08x old=%08x new=%08x writeAddr=%08x writeSize=%u raw=%08x pc=%08x lr=%08x last=%08x tick=%u regs=%08x,%08x,%08x,%08x activeScreen=%08x currentThis=%08x count=%u evidence=Battle.cbm_headerInt2_0x14000\n",
                 label,
                 battleR9,
                 watchedSlot,
                 oldValue,
                 newValue,
                 writeStart,
                 size,
                 (u32)value,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_battleDataWriteTraceCount);
}

void vm_net_trace_battle_main_gate_write(uint64_t address, uint32_t size, int64_t value)
{
    static u32 s_battleMainGateWriteTraceCount = 0;
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    u32 battleR9 = 0;
    u32 mainObj = 0;
    u32 gateBase = 0;
    u32 gateEnd = 0;
    u32 pc = 0, lr = 0;
    u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0;
    u8 gate0 = 0, gate1 = 0, gate2 = 0, gate3 = 0, gate4 = 0;
    u8 newByte = 0xff;

    if (size == 0 || writeEnd < writeStart || s_battleMainGateWriteTraceCount >= 160)
        return;

    battleR9 = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (battleR9 == 0)
    {
        u32 inferredCodeBase = 0;
        (void)vm_infer_battle_module_from_screen(vmAddedScreen, &inferredCodeBase, &battleR9);
    }
    if (battleR9 == 0)
        return;
    (void)vm_net_trace_read_u32(battleR9 + 0x2050u, &mainObj);
    if (mainObj == 0)
        return;

    gateBase = mainObj + 0x470u;
    gateEnd = gateBase + 5u;
    if (writeEnd <= gateBase || writeStart >= gateEnd)
        return;

    (void)vm_net_trace_read_u8(gateBase + 0u, &gate0);
    (void)vm_net_trace_read_u8(gateBase + 1u, &gate1);
    (void)vm_net_trace_read_u8(gateBase + 2u, &gate2);
    (void)vm_net_trace_read_u8(gateBase + 3u, &gate3);
    (void)vm_net_trace_read_u8(gateBase + 4u, &gate4);
    if (writeStart <= gateBase + 4u && writeEnd > gateBase + 4u)
        newByte = (u8)(((u64)value >> ((gateBase + 4u - writeStart) * 8u)) & 0xffu);
    else if (writeStart >= gateBase && writeStart < gateEnd)
        newByte = (u8)(value & 0xffu);

    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);

    ++s_battleMainGateWriteTraceCount;
    vm_net_trace("trace_battle_main_gate_write mainObj=%08x gateBase=%08x oldBytes=%u,%u,%u,%u,%u writeAddr=%08x writeSize=%u raw=%08x newByte=%u pc=%08x lr=%08x last=%08x tick=%u regs=%08x,%08x,%08x,%08x activeScreen=%08x currentThis=%08x count=%u evidence=runtime:Battle.cbm_sub_7BD0_result1_gate_reads_mainObj_0x470_0x474\n",
                 mainObj,
                 gateBase,
                 (unsigned int)gate0,
                 (unsigned int)gate1,
                 (unsigned int)gate2,
                 (unsigned int)gate3,
                 (unsigned int)gate4,
                 writeStart,
                 size,
                 (u32)value,
                 (unsigned int)newByte,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_battleMainGateWriteTraceCount);
}

static void vm_net_trace_status_tile_slots_if_needed(u32 pc)
{
    static u32 s_prevTileInfoBase = 0xffffffffu;
    static u8 s_prevStatus15 = 0xffu;
    static u8 s_prevStatus16 = 0xffu;
    static u8 s_prevStatus17 = 0xffu;
    static const u32 s_tileIds[] = {25, 26, 27, 28, 29, 30, 31, 65};
    static int s_prevTileIndex[8] = {
        0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff,
        0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff};
    static u32 s_prevResPtr[8] = {
        0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
        0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    u32 mapMgr = 0;
    u32 tileInfoBase = 0;
    u32 resourceTable = 0;
    u32 resPtr[8] = {0};
    int tileIndex[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    u8 status15 = 0;
    u8 status16 = 0;
    u8 status17 = 0;
    u32 lr = 0;
    int i = 0;

    if (!Global_R9)
        return;

    uc_mem_read(MTK, Global_R9 + 23673, &status15, 1);
    uc_mem_read(MTK, Global_R9 + 23674, &status16, 1);
    uc_mem_read(MTK, Global_R9 + 23675, &status17, 1);
    uc_mem_read(MTK, Global_R9 + 39664, &mapMgr, 4);
    if (mapMgr)
    {
        uc_mem_read(MTK, mapMgr + 4, &tileInfoBase, 4);
        uc_mem_read(MTK, mapMgr, &resourceTable, 4);
        if (resourceTable)
            uc_mem_read(MTK, resourceTable + 0x10, &resourceTable, 4);
    }
    if (tileInfoBase)
    {
        for (i = 0; i < (int)(sizeof(s_tileIds) / sizeof(s_tileIds[0])); ++i)
        {
            short signedIndex = -1;
            uc_mem_read(MTK, tileInfoBase + s_tileIds[i] * 8 + 2, &signedIndex, 2);
            tileIndex[i] = (int)signedIndex;
            if (resourceTable && tileIndex[i] >= 0)
                uc_mem_read(MTK, resourceTable + (u32)tileIndex[i] * 4, &resPtr[i], 4);
        }
    }
    if (tileInfoBase == s_prevTileInfoBase &&
        status15 == s_prevStatus15 &&
        status16 == s_prevStatus16 &&
        status17 == s_prevStatus17)
    {
        int changed = 0;
        for (i = 0; i < (int)(sizeof(s_tileIds) / sizeof(s_tileIds[0])); ++i)
        {
            if (tileIndex[i] != s_prevTileIndex[i] || resPtr[i] != s_prevResPtr[i])
            {
                changed = 1;
                break;
            }
        }
        if (!changed)
            return;
    }

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    s_prevTileInfoBase = tileInfoBase;
    s_prevStatus15 = status15;
    s_prevStatus16 = status16;
    s_prevStatus17 = status17;
    for (i = 0; i < (int)(sizeof(s_tileIds) / sizeof(s_tileIds[0])); ++i)
    {
        s_prevTileIndex[i] = tileIndex[i];
        s_prevResPtr[i] = resPtr[i];
    }
    vm_net_trace("trace_status_tile_slots pc=%08x lr=%08x last=%08x tick=%u mapMgr=%08x tileInfo=%08x flags=%u,%u,%u"
                 " 25=%d/%08x 26=%d/%08x 27=%d/%08x 28=%d/%08x 29=%d/%08x 30=%d/%08x 31=%d/%08x 65=%d/%08x"
                 " activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 mapMgr,
                 tileInfoBase,
                 (u32)status15,
                 (u32)status16,
                 (u32)status17,
                 tileIndex[0],
                 resPtr[0],
                 tileIndex[1],
                 resPtr[1],
                 tileIndex[2],
                 resPtr[2],
                 tileIndex[3],
                 resPtr[3],
                 tileIndex[4],
                 resPtr[4],
                 tileIndex[5],
                 resPtr[5],
                 tileIndex[6],
                 resPtr[6],
                 tileIndex[7],
                 resPtr[7],
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_status_bar_divide_site(u32 pc)
{
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 statusMeterNode = 0;
    u32 currentValue = 0;
    u32 baseMax = 0;
    u32 displayMax = 0;
    u32 lr = 0;
    const char *label = "unknown";

    if (!Global_R9)
        return;

    hudState = Global_R9 + 0x5C64;

    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x48, &statusMeterNode, 4);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (pc == 0x101477E)
    {
        label = "primary";
        if (currentSceneNode)
        {
            uc_mem_read(MTK, currentSceneNode + 0xB4, &currentValue, 4);
            uc_mem_read(MTK, currentSceneNode + 0xBC, &baseMax, 4);
        }
        if (statusMeterNode)
            uc_mem_read(MTK, statusMeterNode + 0xC4, &displayMax, 4);
    }
    else if (pc == 0x10147AE)
    {
        label = "secondary";
        if (currentSceneNode)
        {
            uc_mem_read(MTK, currentSceneNode + 0xB8, &currentValue, 4);
            uc_mem_read(MTK, currentSceneNode + 0xC0, &baseMax, 4);
        }
        if (statusMeterNode)
            uc_mem_read(MTK, statusMeterNode + 0xC8, &displayMax, 4);
    }

    vm_net_trace("trace_status_bar_divide_site label=%s pc=%08x lr=%08x last=%08x tick=%u currentNode=%08x meterNode=%08x current=%u baseMax=%u displayMax=%u activeScreen=%08x currentThis=%08x\n",
                 label,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 statusMeterNode,
                 currentValue,
                 baseMax,
                 displayMax,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_status_bar_state_if_needed(u32 pc)
{
    static u32 s_prevCurrentNode = 0xffffffffu;
    static u32 s_prevMeterNode = 0xffffffffu;
    static u32 s_prevPrimaryCurrent = 0xffffffffu;
    static u32 s_prevSecondaryCurrent = 0xffffffffu;
    static u32 s_prevPrimaryBaseMax = 0xffffffffu;
    static u32 s_prevSecondaryBaseMax = 0xffffffffu;
    static u32 s_prevPrimaryDisplayMax = 0xffffffffu;
    static u32 s_prevSecondaryDisplayMax = 0xffffffffu;
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 statusMeterNode = 0;
    u32 primaryCurrent = 0;
    u32 secondaryCurrent = 0;
    u32 primaryBaseMax = 0;
    u32 secondaryBaseMax = 0;
    u32 primaryDisplayMax = 0;
    u32 secondaryDisplayMax = 0;
    u32 lr = 0;

    if (!Global_R9)
        return;

    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x48, &statusMeterNode, 4);
    if (currentSceneNode)
    {
        uc_mem_read(MTK, currentSceneNode + 0xB4, &primaryCurrent, 4);
        uc_mem_read(MTK, currentSceneNode + 0xB8, &secondaryCurrent, 4);
        uc_mem_read(MTK, currentSceneNode + 0xBC, &primaryBaseMax, 4);
        uc_mem_read(MTK, currentSceneNode + 0xC0, &secondaryBaseMax, 4);
    }
    if (statusMeterNode)
    {
        uc_mem_read(MTK, statusMeterNode + 0xC4, &primaryDisplayMax, 4);
        uc_mem_read(MTK, statusMeterNode + 0xC8, &secondaryDisplayMax, 4);
    }

    if (currentSceneNode == s_prevCurrentNode &&
        statusMeterNode == s_prevMeterNode &&
        primaryCurrent == s_prevPrimaryCurrent &&
        secondaryCurrent == s_prevSecondaryCurrent &&
        primaryBaseMax == s_prevPrimaryBaseMax &&
        secondaryBaseMax == s_prevSecondaryBaseMax &&
        primaryDisplayMax == s_prevPrimaryDisplayMax &&
        secondaryDisplayMax == s_prevSecondaryDisplayMax)
    {
        return;
    }

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    s_prevCurrentNode = currentSceneNode;
    s_prevMeterNode = statusMeterNode;
    s_prevPrimaryCurrent = primaryCurrent;
    s_prevSecondaryCurrent = secondaryCurrent;
    s_prevPrimaryBaseMax = primaryBaseMax;
    s_prevSecondaryBaseMax = secondaryBaseMax;
    s_prevPrimaryDisplayMax = primaryDisplayMax;
    s_prevSecondaryDisplayMax = secondaryDisplayMax;

    vm_net_trace("trace_status_bar_state pc=%08x lr=%08x last=%08x tick=%u currentNode=%08x meterNode=%08x primary=%u/%u/%u secondary=%u/%u/%u activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 statusMeterNode,
                 primaryCurrent,
                 primaryBaseMax,
                 primaryDisplayMax,
                 secondaryCurrent,
                 secondaryBaseMax,
                 secondaryDisplayMax,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_scene_actorinfo_snapshot(u32 pc)
{
    u32 currentSceneNode = 0;
    u32 actorId = 0;
    u32 summaryId = 0;
    u32 primaryCurrent = 0;
    u32 secondaryCurrent = 0;
    u32 primaryBaseMax = 0;
    u32 secondaryBaseMax = 0;
    u32 primaryDisplayMax = 0;
    u32 secondaryDisplayMax = 0;
    u32 actorGap0CC0 = 0;
    u32 actorGap0CC4 = 0;
    u32 actorGap0CC8 = 0;
    u32 lr = 0;
    u16 summaryStatus = 0;
    char labelText[64];
    char shortLabel[64];
    char previewImage[64];
    char actorResource[64];
    char sceneKey[64];

    if (!Global_R9)
        return;

    memset(labelText, 0, sizeof(labelText));
    memset(shortLabel, 0, sizeof(shortLabel));
    memset(previewImage, 0, sizeof(previewImage));
    memset(actorResource, 0, sizeof(actorResource));
    memset(sceneKey, 0, sizeof(sceneKey));

    if (uc_mem_read(MTK, Global_R9 + 23720, &currentSceneNode, 4) != UC_ERR_OK || currentSceneNode == 0)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, currentSceneNode + 100, &actorId, 4);
    uc_mem_read(MTK, currentSceneNode + 104, &summaryId, 4);
    uc_mem_read(MTK, currentSceneNode + 180, &primaryCurrent, 4);
    uc_mem_read(MTK, currentSceneNode + 184, &secondaryCurrent, 4);
    uc_mem_read(MTK, currentSceneNode + 188, &primaryBaseMax, 4);
    uc_mem_read(MTK, currentSceneNode + 192, &secondaryBaseMax, 4);
    uc_mem_read(MTK, currentSceneNode + 196, &primaryDisplayMax, 4);
    uc_mem_read(MTK, currentSceneNode + 200, &secondaryDisplayMax, 4);
    uc_mem_read(MTK, currentSceneNode + 204, &actorGap0CC0, 4);
    uc_mem_read(MTK, currentSceneNode + 208, &actorGap0CC4, 4);
    uc_mem_read(MTK, currentSceneNode + 212, &actorGap0CC8, 4);
    uc_mem_read(MTK, currentSceneNode + 312, &summaryStatus, 2);
    vm_net_read_guest_best_effort_label(currentSceneNode + 108, labelText, sizeof(labelText));
    vm_net_read_guest_best_effort_label(currentSceneNode + 256, shortLabel, sizeof(shortLabel));
    vm_net_read_guest_best_effort_label(currentSceneNode + 266, previewImage, sizeof(previewImage));
    vm_net_read_guest_best_effort_label(currentSceneNode + 216, actorResource, sizeof(actorResource));
    vm_net_read_guest_best_effort_label(Global_R9 + 24134, sceneKey, sizeof(sceneKey));

    vm_net_trace("trace_scene_actorinfo_snapshot pc=%08x lr=%08x last=%08x tick=%u node=%08x actorId=%u summaryId=%u summaryStatus=%u label=%s short=%s preview=%s actor=%s scene=%s hp=%u/%u mp=%u/%u extraCC=%u,%u,%u activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 actorId,
                 summaryId,
                 (unsigned int)summaryStatus,
                 labelText,
                 shortLabel,
                 previewImage,
                 actorResource,
                 sceneKey,
                 primaryCurrent,
                 primaryBaseMax,
                 secondaryCurrent,
                 secondaryBaseMax,
                 actorGap0CC0,
                 actorGap0CC4,
                 actorGap0CC8,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_scene_current_node_publish(u32 pc)
{
    static u32 s_sceneCurrentNodePublishTraceCount = 0;
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 snapshotNode = 0;
    u32 lr = 0;
    u32 currentActorId = 0;
    u32 snapshotActorId = 0;
    u32 currentDrawAt = 0;
    u32 currentStep = 0;
    u32 currentVisualResId = 0;
    u32 snapshotVisualResId = 0;
    u16 currentGridX = 0;
    u16 currentGridY = 0;
    u16 currentTargetX = 0;
    u16 currentTargetY = 0;
    u8 currentOccupied = 0;
    u8 currentNodeKind = 0;
    u8 currentVisualGroup = 0;
    u8 currentVisualVariant = 0;
    char currentLabel[64];
    char currentShort[32];
    char snapshotLabel[64];

    if (!Global_R9 || s_sceneCurrentNodePublishTraceCount >= 16)
        return;
    ++s_sceneCurrentNodePublishTraceCount;

    memset(currentLabel, 0, sizeof(currentLabel));
    memset(currentShort, 0, sizeof(currentShort));
    memset(snapshotLabel, 0, sizeof(snapshotLabel));

    hudState = Global_R9 + 0x5C64;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x44, &snapshotNode, 4);

    if (currentSceneNode)
    {
        uc_mem_read(MTK, currentSceneNode + 0x64, &currentActorId, 4);
        uc_mem_read(MTK, currentSceneNode + 0x24, &currentVisualResId, 4);
        uc_mem_read(MTK, currentSceneNode + 0x18, &currentGridX, 2);
        uc_mem_read(MTK, currentSceneNode + 0x1A, &currentGridY, 2);
        uc_mem_read(MTK, currentSceneNode + 0x11E, &currentTargetX, 2);
        uc_mem_read(MTK, currentSceneNode + 0x120, &currentTargetY, 2);
        uc_mem_read(MTK, currentSceneNode + 0x13B, &currentNodeKind, 1);
        uc_mem_read(MTK, currentSceneNode + 0x13F, &currentOccupied, 1);
        uc_mem_read(MTK, currentSceneNode + 0x140, &currentVisualGroup, 1);
        uc_mem_read(MTK, currentSceneNode + 0x141, &currentVisualVariant, 1);
        uc_mem_read(MTK, currentSceneNode + 0x148, &currentDrawAt, 4);
        uc_mem_read(MTK, currentSceneNode + 0x14C, &currentStep, 4);
        vm_net_read_guest_best_effort_label(currentSceneNode + 0x44, currentLabel, sizeof(currentLabel));
        vm_net_read_guest_best_effort_label(currentSceneNode + 0x100, currentShort, sizeof(currentShort));
    }
    if (snapshotNode)
    {
        uc_mem_read(MTK, snapshotNode + 0x64, &snapshotActorId, 4);
        uc_mem_read(MTK, snapshotNode + 0x24, &snapshotVisualResId, 4);
        vm_net_read_guest_best_effort_label(snapshotNode + 0x44, snapshotLabel, sizeof(snapshotLabel));
    }

    vm_net_trace("trace_scene_current_node_publish pc=%08x lr=%08x last=%08x tick=%u current=%08x snapshot=%08x"
                 " actor=%u/%u label=%s snapshotLabel=%s short=%s grid=%u,%u target=%u,%u"
                 " visual=%u,%u visualRes=%u/%u nodeKind=%u occupied=%u drawAt=%08x step=%08x"
                 " activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 snapshotNode,
                 currentActorId,
                 snapshotActorId,
                 currentLabel,
                 snapshotLabel,
                 currentShort,
                 (unsigned int)currentGridX,
                 (unsigned int)currentGridY,
                 (unsigned int)currentTargetX,
                 (unsigned int)currentTargetY,
                 (unsigned int)currentVisualGroup,
                 (unsigned int)currentVisualVariant,
                 currentVisualResId,
                 snapshotVisualResId,
                 (unsigned int)currentNodeKind,
                 (unsigned int)currentOccupied,
                 currentDrawAt,
                 currentStep,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneCurrentNodePublishTraceCount);
}

static void vm_fixup_current_node_visual_res_if_ready(u32 pc, const char *label)
{
    static u32 s_sceneVisualResFixupTraceCount = 0;
    static u32 s_sceneVisualResMissingTraceCount = 0;
    static u32 s_sceneVisualResProbeTraceCount = 0;
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 visualResId = 0;
    u32 tableEntry = 0;
    u32 selectedIndex = 0;
    u32 slotEntries[6] = {0};
    u8 visualGroup = 0;
    u8 visualVariant = 0;

    if (!Global_R9)
    {
        if (s_sceneVisualResProbeTraceCount < 24)
        {
            ++s_sceneVisualResProbeTraceCount;
            vm_net_trace("trace_scene_visual_res_probe label=%s pc=%08x last=%08x tick=%u"
                         " reason=no_r9 activeScreen=%08x currentThis=%08x count=%u\n",
                         label ? label : "unknown",
                         pc,
                         lastAddress,
                         g_schedulerTick,
                         vmAddedScreen,
                         g_currentScreenThis,
                         s_sceneVisualResProbeTraceCount);
        }
        return;
    }

    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    if (!currentSceneNode)
    {
        if (s_sceneVisualResProbeTraceCount < 24)
        {
            ++s_sceneVisualResProbeTraceCount;
            vm_net_trace("trace_scene_visual_res_probe label=%s pc=%08x last=%08x tick=%u"
                         " reason=no_current_node hudState=%08x activeScreen=%08x currentThis=%08x count=%u\n",
                         label ? label : "unknown",
                         pc,
                         lastAddress,
                         g_schedulerTick,
                         hudState,
                         vmAddedScreen,
                         g_currentScreenThis,
                         s_sceneVisualResProbeTraceCount);
        }
        return;
    }

    uc_mem_read(MTK, currentSceneNode + 0x24, &visualResId, 4);
    uc_mem_read(MTK, currentSceneNode + 0x140, &visualGroup, 1);
    uc_mem_read(MTK, currentSceneNode + 0x141, &visualVariant, 1);
    if (visualGroup == 0 || visualGroup > 2 || visualVariant > 2)
    {
        if (s_sceneVisualResProbeTraceCount < 24)
        {
            ++s_sceneVisualResProbeTraceCount;
            vm_net_trace("trace_scene_visual_res_probe label=%s pc=%08x last=%08x tick=%u"
                         " reason=invalid_visual node=%08x visual=%u,%u visualRes=%08x"
                         " activeScreen=%08x currentThis=%08x count=%u\n",
                         label ? label : "unknown",
                         pc,
                         lastAddress,
                         g_schedulerTick,
                         currentSceneNode,
                         (unsigned int)visualGroup,
                         (unsigned int)visualVariant,
                         visualResId,
                         vmAddedScreen,
                         g_currentScreenThis,
                         s_sceneVisualResProbeTraceCount);
        }
        return;
    }

    selectedIndex = (u32)visualGroup + ((u32)visualVariant << 1);
    uc_mem_read(MTK, currentSceneNode + 0x00, &slotEntries[0], sizeof(slotEntries));
    uc_mem_read(MTK, currentSceneNode + 4 * (selectedIndex - 1), &tableEntry, 4);
    if (s_sceneVisualResProbeTraceCount < 24)
    {
        ++s_sceneVisualResProbeTraceCount;
        vm_net_trace("trace_scene_visual_res_probe label=%s pc=%08x last=%08x tick=%u"
                     " reason=ready node=%08x visual=%u,%u selectedIndex=%u visualRes=%08x tableEntry=%08x"
                     " slots=%08x,%08x,%08x,%08x,%08x,%08x activeScreen=%08x currentThis=%08x count=%u\n",
                     label ? label : "unknown",
                     pc,
                     lastAddress,
                     g_schedulerTick,
                     currentSceneNode,
                     (unsigned int)visualGroup,
                     (unsigned int)visualVariant,
                     selectedIndex,
                     visualResId,
                     tableEntry,
                     slotEntries[0],
                     slotEntries[1],
                     slotEntries[2],
                     slotEntries[3],
                     slotEntries[4],
                     slotEntries[5],
                     vmAddedScreen,
                     g_currentScreenThis,
                     s_sceneVisualResProbeTraceCount);
    }

    if (visualResId == 0 && tableEntry != 0)
    {
        uc_mem_write(MTK, currentSceneNode + 0x24, &tableEntry, 4);
        if (s_sceneVisualResFixupTraceCount < 24)
        {
            ++s_sceneVisualResFixupTraceCount;
            vm_net_trace("trace_scene_visual_res_fixup label=%s pc=%08x last=%08x tick=%u node=%08x"
                         " visual=%u,%u selectedIndex=%u tableEntry=%08x oldVisualRes=%08x"
                         " activeScreen=%08x currentThis=%08x count=%u\n",
                         label ? label : "unknown",
                         pc,
                         lastAddress,
                         g_schedulerTick,
                         currentSceneNode,
                         (unsigned int)visualGroup,
                         (unsigned int)visualVariant,
                         selectedIndex,
                         tableEntry,
                         visualResId,
                         vmAddedScreen,
                         g_currentScreenThis,
                         s_sceneVisualResFixupTraceCount);
        }
    }
    else if (visualResId == 0 && tableEntry == 0 && s_sceneVisualResMissingTraceCount < 24)
    {
        ++s_sceneVisualResMissingTraceCount;
        vm_net_trace("trace_scene_visual_res_still_missing label=%s pc=%08x last=%08x tick=%u node=%08x"
                     " visual=%u,%u selectedIndex=%u visualRes=%08x tableEntry=%08x"
                     " activeScreen=%08x currentThis=%08x count=%u\n",
                     label ? label : "unknown",
                     pc,
                     lastAddress,
                     g_schedulerTick,
                     currentSceneNode,
                     (unsigned int)visualGroup,
                     (unsigned int)visualVariant,
                     selectedIndex,
                     visualResId,
                     tableEntry,
                     vmAddedScreen,
                     g_currentScreenThis,
                     s_sceneVisualResMissingTraceCount);
    }
}

static void vm_trace_scene_body_draw_dispatch(const char *label, u32 pc)
{
    static u32 s_sceneBodyDrawTraceCount = 0;
    u32 moveEntriesBase = 0;
    u32 moveEntryPtr = 0;
    u32 callbackObj = 0;
    u32 callbackFn = 0;
    u32 lr = 0;
    u32 r4 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u16 raw0 = 0;
    u16 raw2 = 0;
    u16 raw6 = 0;
    u16 raw8 = 0;
    u16 rawA = 0;
    u16 rawC = 0;
    u8 entryKind = 0;

    if (!Global_R9 || s_sceneBodyDrawTraceCount >= 24)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R0, &callbackObj);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &callbackFn);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (uc_mem_read(MTK, Global_R9 + 0x5CE4, &moveEntriesBase, 4) != UC_ERR_OK || moveEntriesBase == 0)
        return;

    moveEntryPtr = moveEntriesBase + ((r4 & 0xFFFFu) * 32u);
    uc_mem_read(MTK, moveEntryPtr + 0x00, &raw0, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x02, &raw2, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x06, &raw6, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x08, &raw8, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x0A, &rawA, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x0C, &rawC, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x17, &entryKind, 1);

    ++s_sceneBodyDrawTraceCount;
    vm_net_trace("trace_scene_body_draw_dispatch label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " moveIndex=%u entry=%08x kind=%u raw=%u,%u box=%u,%u,%u,%u"
                 " screen=%d,%d callbackObj=%08x callbackFn=%08x activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 (unsigned int)(r4 & 0xFFFFu),
                 moveEntryPtr,
                 (unsigned int)entryKind,
                 (unsigned int)raw0,
                 (unsigned int)raw2,
                 (unsigned int)raw6,
                 (unsigned int)raw8,
                 (unsigned int)rawA,
                 (unsigned int)rawC,
                 (int)r1,
                 (int)r2,
                 callbackObj,
                 callbackFn,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneBodyDrawTraceCount);
}

static void vm_trace_scene_current_node_draw_callbacks(const char *label, u32 pc)
{
    static u32 s_sceneCurrentNodeDrawTraceCount = 0;
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 nodePtr = 0;
    u32 callbackFn = 0;
    u32 lr = 0;
    u32 actorId = 0;
    u32 visualResId = 0;
    u32 drawAt = 0;
    u32 step = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 targetX = 0;
    u16 targetY = 0;
    u8 visualGroup = 0;
    u8 visualVariant = 0;

    if (!Global_R9 || s_sceneCurrentNodeDrawTraceCount >= 24)
        return;

    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_reg_read(MTK, UC_ARM_REG_R0, &nodePtr);
    if (!currentSceneNode || nodePtr != currentSceneNode)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, currentSceneNode + 0x64, &actorId, 4);
    uc_mem_read(MTK, currentSceneNode + 0x24, &visualResId, 4);
    uc_mem_read(MTK, currentSceneNode + 0x18, &gridX, 2);
    uc_mem_read(MTK, currentSceneNode + 0x1A, &gridY, 2);
    uc_mem_read(MTK, currentSceneNode + 0x11E, &targetX, 2);
    uc_mem_read(MTK, currentSceneNode + 0x120, &targetY, 2);
    uc_mem_read(MTK, currentSceneNode + 0x140, &visualGroup, 1);
    uc_mem_read(MTK, currentSceneNode + 0x141, &visualVariant, 1);
    uc_mem_read(MTK, currentSceneNode + 0x148, &drawAt, 4);
    uc_mem_read(MTK, currentSceneNode + 0x14C, &step, 4);
    if (pc == 0x1014594)
        callbackFn = drawAt;
    else
        callbackFn = step;

    ++s_sceneCurrentNodeDrawTraceCount;
    vm_net_trace("trace_scene_current_node_draw label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " node=%08x actorId=%u visual=%u,%u visualRes=%08x grid=%u,%u target=%u,%u"
                 " screen=%d,%d callback=%08x drawAt=%08x step=%08x activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 actorId,
                 (unsigned int)visualGroup,
                 (unsigned int)visualVariant,
                 visualResId,
                 (unsigned int)gridX,
                 (unsigned int)gridY,
                 (unsigned int)targetX,
                 (unsigned int)targetY,
                 (int)r1,
                 (int)r2,
                 callbackFn,
                 drawAt,
                 step,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneCurrentNodeDrawTraceCount);
}

static void vm_trace_actor_move_entry_table(u32 pc)
{
    static u32 s_actorMoveEntryTableTraceCount = 0;
    u32 tableBasePtr = 0;
    u32 entryCount = 0;
    u32 lr = 0;
    u32 hudState = 0;
    u32 promptNamePtr = 0;
    u32 statusTextPtr = 0;
    char promptName[80];
    char statusText[80];

    if (!Global_R9 || s_actorMoveEntryTableTraceCount >= 8)
        return;

    memset(promptName, 0, sizeof(promptName));
    memset(statusText, 0, sizeof(statusText));
    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x74, &promptNamePtr, 4);
    uc_mem_read(MTK, hudState + 0x78, &statusTextPtr, 4);
    vm_net_read_guest_best_effort_label(promptNamePtr, promptName, sizeof(promptName));
    vm_net_read_guest_best_effort_label(statusTextPtr, statusText, sizeof(statusText));

    if (uc_mem_read(MTK, Global_R9 + 0x5CE4, &tableBasePtr, 4) != UC_ERR_OK || tableBasePtr == 0)
        return;
    if (uc_mem_read(MTK, Global_R9 + 0x5D40, &entryCount, 4) != UC_ERR_OK)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    ++s_actorMoveEntryTableTraceCount;
    vm_net_trace("trace_actor_move_entry_table pc=%08x lr=%08x last=%08x tick=%u table=%08x count=%u promptNamePtr=%08x promptName=%s statusTextPtr=%08x statusText=%s activeScreen=%08x currentThis=%08x snap=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 tableBasePtr,
                 entryCount,
                 promptNamePtr,
                 promptName,
                 statusTextPtr,
                 statusText,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMoveEntryTableTraceCount);

    for (u32 i = 0; i < entryCount && i < 4; ++i)
    {
        u32 entryPtr = tableBasePtr + i * 32u;
        u32 d[8] = {0};
        u8 kind = 0;
        uc_mem_read(MTK, entryPtr, d, sizeof(d));
        uc_mem_read(MTK, entryPtr + 0x17, &kind, 1);
        vm_net_trace("trace_actor_move_entry item=%u entry=%08x dwords=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x kind=%u activeScreen=%08x currentThis=%08x\n",
                     i,
                     entryPtr,
                     d[0],
                     d[1],
                     d[2],
                     d[3],
                     d[4],
                     d[5],
                     d[6],
                     d[7],
                     (unsigned int)kind,
                     vmAddedScreen,
                     g_currentScreenThis);
    }
}

static void vm_trace_scene_status_text_write(const char *fieldName, u32 pc)
{
    static u32 s_sceneStatusTextWriteTraceCount = 0;
    u32 lr = 0;
    u32 textPtr = 0;
    u32 promptNamePtr = 0;
    u32 statusTextPtr = 0;
    char text[96];
    char promptName[80];
    char statusText[80];

    if (!Global_R9 || s_sceneStatusTextWriteTraceCount >= 16)
        return;

    memset(text, 0, sizeof(text));
    memset(promptName, 0, sizeof(promptName));
    memset(statusText, 0, sizeof(statusText));
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &textPtr);
    uc_mem_read(MTK, Global_R9 + 0x5CD8, &promptNamePtr, 4);
    uc_mem_read(MTK, Global_R9 + 0x5CDC, &statusTextPtr, 4);
    vm_net_read_guest_best_effort_label(textPtr, text, sizeof(text));
    vm_net_read_guest_best_effort_label(promptNamePtr, promptName, sizeof(promptName));
    vm_net_read_guest_best_effort_label(statusTextPtr, statusText, sizeof(statusText));

    ++s_sceneStatusTextWriteTraceCount;
    vm_net_trace("trace_scene_status_text_write field=%s pc=%08x lr=%08x last=%08x tick=%u newPtr=%08x newText=%s oldPromptPtr=%08x oldPrompt=%s oldStatusPtr=%08x oldStatus=%s activeScreen=%08x currentThis=%08x count=%u\n",
                 fieldName ? fieldName : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 textPtr,
                 text,
                 promptNamePtr,
                 promptName,
                 statusTextPtr,
                 statusText,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneStatusTextWriteTraceCount);
}

static void vm_net_trace_prompt_hotspot_candidate(const char *label, u32 pc)
{
    static u32 s_promptHotspotTraceCount = 0;
    u32 lr = 0, sp = 0;
    u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0;
    u32 hudState = 0;
    u32 table = 0;
    u32 currentNode = 0;
    u32 promptNamePtr = 0;
    u32 statusTextPtr = 0;
    u32 record = 0;
    u32 recordFromIndex = 0;
    u32 recordId = 0;
    u32 recordPtr40 = 0;
    u32 recordPtr44 = 0;
    u32 recordD130 = 0;
    u8 recordB13b = 0;
    u8 recordB13f = 0;
    u8 recordB13c = 0;
    u8 recordB140 = 0;
    u32 recordWords[8] = {0};
    char promptName[96];
    char statusText[96];
    char recordLabel[96];
    u32 ids[8] = {0};
    u8 flags13b[8] = {0};
    u8 flags13f[8] = {0};

    if (!Global_R9 || s_promptHotspotTraceCount >= 160)
        return;

    memset(promptName, 0, sizeof(promptName));
    memset(statusText, 0, sizeof(statusText));
    memset(recordLabel, 0, sizeof(recordLabel));
    hudState = Global_R9 + 0x5C64u;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);

    (void)uc_mem_read(MTK, hudState + 0x40u, &currentNode, 4);
    (void)uc_mem_read(MTK, hudState + 0x4Cu, &table, 4);
    (void)uc_mem_read(MTK, hudState + 0x74u, &promptNamePtr, 4);
    (void)uc_mem_read(MTK, hudState + 0x78u, &statusTextPtr, 4);
    vm_net_read_guest_best_effort_label(promptNamePtr, promptName, sizeof(promptName));
    vm_net_read_guest_best_effort_label(statusTextPtr, statusText, sizeof(statusText));

    if (table && r4 < 25)
        recordFromIndex = table + r4 * 0x154u;
    if (r1 >= 0x01000000u && r1 < 0x08000000u)
        record = r1;
    else
        record = recordFromIndex;

    if (record)
    {
        (void)uc_mem_read(MTK, record + 0x18u, recordWords, sizeof(recordWords));
        (void)uc_mem_read(MTK, record + 0x40u, &recordPtr40, 4);
        (void)uc_mem_read(MTK, record + 0x44u, &recordPtr44, 4);
        (void)uc_mem_read(MTK, record + 0x64u, &recordId, 4);
        (void)uc_mem_read(MTK, record + 0x130u, &recordD130, 4);
        (void)uc_mem_read(MTK, record + 0x13Bu, &recordB13b, 1);
        (void)uc_mem_read(MTK, record + 0x13Cu, &recordB13c, 1);
        (void)uc_mem_read(MTK, record + 0x13Fu, &recordB13f, 1);
        (void)uc_mem_read(MTK, record + 0x140u, &recordB140, 1);
        vm_net_read_guest_best_effort_label(recordPtr44, recordLabel, sizeof(recordLabel));
    }
    if (table)
    {
        for (u32 i = 0; i < 8; ++i)
        {
            u32 entry = table + i * 0x154u;
            (void)uc_mem_read(MTK, entry + 0x64u, &ids[i], 4);
            (void)uc_mem_read(MTK, entry + 0x13Bu, &flags13b[i], 1);
            (void)uc_mem_read(MTK, entry + 0x13Fu, &flags13f[i], 1);
        }
    }

    ++s_promptHotspotTraceCount;
    vm_net_trace("trace_prompt_hotspot_candidate label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x hud=%08x table=%08x currentNode=%08x"
                 " prompt=%08x:%s status=%08x:%s record=%08x recordFromIndex=%08x index=%u recordId=%u"
                 " recPtrs=%08x,%08x recLabel=%s rec18=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x rec130=%08x flags=%u,%u,%u,%u"
                 " ids=%u,%u,%u,%u,%u,%u,%u,%u flags13b=%u,%u,%u,%u,%u,%u,%u,%u flags13f=%u,%u,%u,%u,%u,%u,%u,%u"
                 " activeScreen=%08x currentThis=%08x count=%u evidence=main:sub_10183A0_prompt_hotspot_selector\n",
                 label ? label : "unknown", pc, lr, lr & ~1u, lastAddress, g_schedulerTick,
                 r0, r1, r2, r3, r4, r5, r6, r7, sp, hudState, table, currentNode,
                 promptNamePtr, promptName, statusTextPtr, statusText, record, recordFromIndex,
                 (unsigned int)r4, recordId,
                 recordPtr40, recordPtr44, recordLabel,
                 recordWords[0], recordWords[1], recordWords[2], recordWords[3],
                 recordWords[4], recordWords[5], recordWords[6], recordWords[7],
                 recordD130,
                 (unsigned int)recordB13b, (unsigned int)recordB13c,
                 (unsigned int)recordB13f, (unsigned int)recordB140,
                 ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6], ids[7],
                 (unsigned int)flags13b[0], (unsigned int)flags13b[1],
                 (unsigned int)flags13b[2], (unsigned int)flags13b[3],
                 (unsigned int)flags13b[4], (unsigned int)flags13b[5],
                 (unsigned int)flags13b[6], (unsigned int)flags13b[7],
                 (unsigned int)flags13f[0], (unsigned int)flags13f[1],
                 (unsigned int)flags13f[2], (unsigned int)flags13f[3],
                 (unsigned int)flags13f[4], (unsigned int)flags13f[5],
                 (unsigned int)flags13f[6], (unsigned int)flags13f[7],
                 vmAddedScreen, g_currentScreenThis, s_promptHotspotTraceCount);
}

static void vm_trace_actor_move_entry_parser_entry(u32 pc)
{
    static u32 s_actorMoveEntryParserEntryTraceCount = 0;
    u32 lr = 0;
    u32 streamBase = 0;
    u32 posPtr = 0;
    u32 cursor = 0;

    if (s_actorMoveEntryParserEntryTraceCount >= 8)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &streamBase);
    uc_reg_read(MTK, UC_ARM_REG_R1, &posPtr);
    if (posPtr)
        uc_mem_read(MTK, posPtr, &cursor, 4);

    ++s_actorMoveEntryParserEntryTraceCount;
    vm_net_trace("trace_actor_move_entry_parser_entry pc=%08x lr=%08x caller=%08x last=%08x tick=%u stream=%08x posPtr=%08x cursor=%u activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 streamBase,
                 posPtr,
                 cursor,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMoveEntryParserEntryTraceCount);
}

static void vm_trace_actor_motion_descriptor_context(u32 pc)
{
    static u32 s_actorMotionDescriptorContextTraceCount = 0;
    u32 lr = 0;
    u32 arg0 = 0;
    u32 arg1 = 0;
    u32 arg2 = 0;
    u32 arg3 = 0;
    u32 sp = 0;
    u32 a5 = 0;
    u32 a6 = 0;
    u32 a7 = 0;
    u32 a8 = 0;
    char nameBuf[96];

    if (s_actorMotionDescriptorContextTraceCount >= 8)
        return;

    memset(nameBuf, 0, sizeof(nameBuf));
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &arg0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &arg1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &arg2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &arg3);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_mem_read(MTK, sp + 0x0, &a5, 4);
    uc_mem_read(MTK, sp + 0x4, &a6, 4);
    uc_mem_read(MTK, sp + 0x8, &a7, 4);
    uc_mem_read(MTK, sp + 0xC, &a8, 4);
    if (a5)
        vm_net_read_guest_best_effort_label(a5, nameBuf, sizeof(nameBuf));

    if (g_vm_net_mock_pending_scene_save_valid &&
        nameBuf[0] != 0 &&
        strcmp(nameBuf, g_vm_net_mock_pending_scene_save_scene) == 0)
    {
        vm_net_trace("mock_player_pos_confirm_pending_scene_load scene=%s pos=%u,%u reason=%s tick=%u\n",
                     g_vm_net_mock_pending_scene_save_scene,
                     (unsigned int)g_vm_net_mock_pending_scene_save_x,
                     (unsigned int)g_vm_net_mock_pending_scene_save_y,
                     g_vm_net_mock_pending_scene_save_reason,
                     g_schedulerTick);
        vm_net_mock_save_player_pos_state(g_vm_net_mock_pending_scene_save_scene,
                                          g_vm_net_mock_pending_scene_save_x,
                                          g_vm_net_mock_pending_scene_save_y,
                                          g_vm_net_mock_pending_scene_save_reason);
        g_vm_net_mock_pending_scene_save_valid = false;
    }

    ++s_actorMotionDescriptorContextTraceCount;
    vm_net_trace("trace_actor_motion_descriptor_context pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " r0=%08x r1=%08x r2=%08x r3=%08x a5=%08x name=%s a6=%08x a7=%08x a8=%08x"
                 " activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 arg0,
                 arg1,
                 arg2,
                 arg3,
                 a5,
                 nameBuf,
                 a6,
                 a7,
                 a8,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMotionDescriptorContextTraceCount);
}

static void vm_trace_actor_move_entry_append(u32 pc)
{
    static u32 s_actorMoveEntryAppendTraceCount = 0;
    u32 tableBasePtr = 0;
    u32 entryCount = 0;
    u32 entryPtr = 0;
    u32 lr = 0;
    u32 d[8] = {0};
    u8 kind = 0;

    if (!Global_R9 || s_actorMoveEntryAppendTraceCount >= 16)
        return;
    if (uc_mem_read(MTK, Global_R9 + 0x5CE4, &tableBasePtr, 4) != UC_ERR_OK || tableBasePtr == 0)
        return;
    if (uc_mem_read(MTK, Global_R9 + 0x5D40, &entryCount, 4) != UC_ERR_OK)
        return;

    entryPtr = tableBasePtr + entryCount * 32u;
    uc_mem_read(MTK, entryPtr, d, sizeof(d));
    uc_mem_read(MTK, entryPtr + 0x17, &kind, 1);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    ++s_actorMoveEntryAppendTraceCount;
    vm_net_trace("trace_actor_move_entry_append pc=%08x lr=%08x last=%08x tick=%u table=%08x index=%u entry=%08x dwords=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x kind=%u activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 tableBasePtr,
                 entryCount,
                 entryPtr,
                 d[0],
                 d[1],
                 d[2],
                 d[3],
                 d[4],
                 d[5],
                 d[6],
                 d[7],
                 (unsigned int)kind,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMoveEntryAppendTraceCount);
}

static bool vm_should_skip_portal_move_entry_append(u32 pc)
{
    static u32 s_portalMoveEntrySkipTraceCount = 0;
    u32 tableBasePtr = 0;
    u32 entryCount = 0;
    u32 entryPtr = 0;
    u32 lr = 0;
    u16 actorId = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 boxLeft = 0;
    u16 boxTop = 0;
    u16 boxRight = 0;
    u16 boxBottom = 0;
    u8 kind = 0;

    if (!Global_R9)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5CE4, &tableBasePtr, 4) != UC_ERR_OK || tableBasePtr == 0)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5D40, &entryCount, 4) != UC_ERR_OK)
        return false;

    entryPtr = tableBasePtr + entryCount * 32u;
    uc_mem_read(MTK, entryPtr + 0x00, &actorId, 2);
    uc_mem_read(MTK, entryPtr + 0x02, &gridX, 2);
    uc_mem_read(MTK, entryPtr + 0x04, &gridY, 2);
    uc_mem_read(MTK, entryPtr + 0x06, &boxLeft, 2);
    uc_mem_read(MTK, entryPtr + 0x08, &boxTop, 2);
    uc_mem_read(MTK, entryPtr + 0x0A, &boxRight, 2);
    uc_mem_read(MTK, entryPtr + 0x0C, &boxBottom, 2);
    uc_mem_read(MTK, entryPtr + 0x17, &kind, 1);

    if (actorId != 1 ||
        gridX != 223 ||
        gridY != 382 ||
        boxLeft != 203 ||
        boxTop != 402 ||
        boxRight != 240 ||
        boxBottom != 422 ||
        kind != 2)
    {
        return false;
    }

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (s_portalMoveEntrySkipTraceCount < 16)
    {
        ++s_portalMoveEntrySkipTraceCount;
        vm_net_trace("trace_portal_move_entry_append_skipped pc=%08x lr=%08x last=%08x tick=%u table=%08x index=%u entry=%08x actorId=%u grid=%u,%u box=%u,%u,%u,%u kind=%u activeScreen=%08x currentThis=%08x count=%u\n",
                     pc,
                     lr,
                     lastAddress,
                     g_schedulerTick,
                     tableBasePtr,
                     entryCount,
                     entryPtr,
                     (unsigned int)actorId,
                     (unsigned int)gridX,
                     (unsigned int)gridY,
                     (unsigned int)boxLeft,
                     (unsigned int)boxTop,
                     (unsigned int)boxRight,
                     (unsigned int)boxBottom,
                     (unsigned int)kind,
                     vmAddedScreen,
                     g_currentScreenThis,
                     s_portalMoveEntrySkipTraceCount);
    }
    return true;
}

static void vm_format_trace_bytes_hex(const u8 *bytes, u32 count, char *out, size_t outCap)
{
    u32 i = 0;
    size_t used = 0;

    if (!out || outCap == 0)
        return;
    out[0] = '\0';
    if (!bytes || count == 0)
    {
        snprintf(out, outCap, "empty");
        return;
    }

    for (i = 0; i < count && used + 4 < outCap; ++i)
    {
        int written = snprintf(out + used, outCap - used, "%s%02x", i ? " " : "", bytes[i]);
        if (written <= 0)
            break;
        used += (size_t)written;
        if (used >= outCap)
            break;
    }
}

static void vm_trace_actor_motion_callback_handoff(u32 pc)
{
    static u32 s_actorMotionCallbackHandoffTraceCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 streamBase = 0;
    u32 parserBase = 0;
    u32 parserAux = 0;
    u32 callback = 0;
    u32 cursor = 0;
    u32 tailAddr = 0;
    u32 tuplePtr = 0;
    u32 tail20Ptr = 0;
    u16 count132 = 0;
    u16 countE = 0;
    u16 count8 = 0;
    u16 count600 = 0;
    u16 count20 = 0;
    u16 tailShorts[24] = {0};
    u16 tupleShorts[4] = {0};
    u8 tailBytes[48] = {0};
    u8 tail20Bytes[20] = {0};
    char tailHex[192];
    char tail20Hex[80];

    if (s_actorMotionCallbackHandoffTraceCount >= 8)
        return;

    memset(tailHex, 0, sizeof(tailHex));
    memset(tail20Hex, 0, sizeof(tail20Hex));
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R4, &streamBase);
    uc_reg_read(MTK, UC_ARM_REG_R5, &parserBase);
    uc_reg_read(MTK, UC_ARM_REG_R6, &parserAux);

    if (sp)
    {
        uc_mem_read(MTK, sp + 0x3C, &cursor, 4);
        uc_mem_read(MTK, sp + 0x7C, &callback, 4);
    }
    if (parserBase)
    {
        uc_mem_read(MTK, parserBase + 0x0A, &count132, 2);
        uc_mem_read(MTK, parserBase + 0x0E, &countE, 2);
        uc_mem_read(MTK, parserBase + 0x10, &count8, 2);
        uc_mem_read(MTK, parserBase + 0x12, &count600, 2);
        uc_mem_read(MTK, parserBase + 0x14, &count20, 2);
        uc_mem_read(MTK, parserBase + 1568, &tuplePtr, 4);
        uc_mem_read(MTK, parserBase + 1560, &tail20Ptr, 4);
    }

    if (streamBase)
    {
        tailAddr = streamBase + cursor;
        uc_mem_read(MTK, tailAddr, tailBytes, sizeof(tailBytes));
        uc_mem_read(MTK, tailAddr, tailShorts, sizeof(tailShorts));
        vm_format_trace_bytes_hex(tailBytes, (u32)sizeof(tailBytes), tailHex, sizeof(tailHex));
    }
    if (tuplePtr)
        uc_mem_read(MTK, tuplePtr, tupleShorts, sizeof(tupleShorts));
    if (tail20Ptr)
    {
        uc_mem_read(MTK, tail20Ptr, tail20Bytes, sizeof(tail20Bytes));
        vm_format_trace_bytes_hex(tail20Bytes, (u32)sizeof(tail20Bytes), tail20Hex, sizeof(tail20Hex));
    }

    ++s_actorMotionCallbackHandoffTraceCount;
    vm_net_trace("trace_actor_motion_callback_handoff pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " stream=%08x parser=%08x parserAux=%08x cursor=%u tail=%08x cb=%08x"
                 " counts132=%u countE=%u count8=%u count600=%u count20=%u"
                 " tuplePtr=%08x tuple0=%u,%u,%u,%u"
                 " tail20Ptr=%08x tail20=%s"
                 " tailShorts=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u tailHex=%s"
                 " activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 streamBase,
                 parserBase,
                 parserAux,
                 cursor,
                 tailAddr,
                 callback,
                 (unsigned int)count132,
                 (unsigned int)countE,
                 (unsigned int)count8,
                 (unsigned int)count600,
                 (unsigned int)count20,
                 tuplePtr,
                 (unsigned int)tupleShorts[0],
                 (unsigned int)tupleShorts[1],
                 (unsigned int)tupleShorts[2],
                 (unsigned int)tupleShorts[3],
                 tail20Ptr,
                 tail20Hex,
                 (unsigned int)tailShorts[0],
                 (unsigned int)tailShorts[1],
                 (unsigned int)tailShorts[2],
                 (unsigned int)tailShorts[3],
                 (unsigned int)tailShorts[4],
                 (unsigned int)tailShorts[5],
                 (unsigned int)tailShorts[6],
                 (unsigned int)tailShorts[7],
                 (unsigned int)tailShorts[8],
                 (unsigned int)tailShorts[9],
                 (unsigned int)tailShorts[10],
                 (unsigned int)tailShorts[11],
                 (unsigned int)tailShorts[12],
                 (unsigned int)tailShorts[13],
                 (unsigned int)tailShorts[14],
                 (unsigned int)tailShorts[15],
                 (unsigned int)tailShorts[16],
                 (unsigned int)tailShorts[17],
                 (unsigned int)tailShorts[18],
                 (unsigned int)tailShorts[19],
                 (unsigned int)tailShorts[20],
                 (unsigned int)tailShorts[21],
                 (unsigned int)tailShorts[22],
                 (unsigned int)tailShorts[23],
                 tailHex,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMotionCallbackHandoffTraceCount);
}

static void vm_trace_actor_motion_open_result(u32 pc)
{
    static u32 s_actorMotionOpenResultTraceCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 stream = 0;
    u32 namePtr = 0;
    u32 mode = 0;
    u32 callback = 0;
    char name[128];
    char uiName[96];

    if (s_actorMotionOpenResultTraceCount >= 16)
        return;

    name[0] = '\0';
    uiName[0] = '\0';
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R4, &stream);
    if (sp)
    {
        uc_mem_read(MTK, sp + 0x70, &namePtr, 4);
        uc_mem_read(MTK, sp + 0x74, &mode, 4);
        uc_mem_read(MTK, sp + 0x7C, &callback, 4);
    }
    vm_net_read_guest_best_effort_label(namePtr, name, sizeof(name));
    vm_net_read_active_ui_name(uiName, sizeof(uiName));

    ++s_actorMotionOpenResultTraceCount;
    vm_net_trace("trace_actor_motion_open_result pc=%08x lr=%08x last=%08x tick=%u stream=%08x namePtr=%08x name=%s mode=%u callback=%08x branch=%s uiName=%s activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 stream,
                 namePtr,
                 name,
                 mode,
                 callback,
                 stream ? "local_open_success_direct_parse" : "local_open_fail_enqueue",
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMotionOpenResultTraceCount);
}

static void vm_trace_actor_motion_enqueue_fallback(u32 pc)
{
    static u32 s_actorMotionEnqueueTraceCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 namePtr = 0;
    u32 mode = 0;
    u32 callback = 0;
    u16 managerCount = 0;
    char name[128];
    char uiName[96];

    if (s_actorMotionEnqueueTraceCount >= 16)
        return;

    name[0] = '\0';
    uiName[0] = '\0';
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    if (sp)
    {
        uc_mem_read(MTK, sp + 0x70, &namePtr, 4);
        uc_mem_read(MTK, sp + 0x74, &mode, 4);
        uc_mem_read(MTK, sp + 0x7C, &callback, 4);
    }
    if (Global_R9)
        uc_mem_read(MTK, Global_R9 + 0x9590, &managerCount, 2);
    vm_net_read_guest_best_effort_label(namePtr, name, sizeof(name));
    vm_net_read_active_ui_name(uiName, sizeof(uiName));

    ++s_actorMotionEnqueueTraceCount;
    vm_net_trace("trace_actor_motion_enqueue_fallback pc=%08x lr=%08x last=%08x tick=%u r0=%08x r1=%08x r2=%08x r3=%08x namePtr=%08x name=%s mode=%u callback=%08x managerCountBefore=%u uiName=%s activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 namePtr,
                 name,
                 mode,
                 callback,
                 (unsigned int)managerCount,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMotionEnqueueTraceCount);
}

static void vm_trace_manager_replay_entry(u32 pc)
{
    static u32 s_managerReplayTraceCount = 0;
    u32 lr = 0;
    u32 manager = 0;
    u32 current = 0;
    u16 managerCount = 0;
    u16 recordType = 0;
    u8 callbackChoice = 0;
    u32 currentActorName = 0;
    u32 vargR3 = 0;
    u32 n19202288 = 0;
    u32 contextA = 0;
    u32 contextB = 0;
    u8 nameBytes[30] = {0};
    char nameHex[128];
    char name[128];

    if (s_managerReplayTraceCount >= 16 || !Global_R9)
        return;

    nameHex[0] = '\0';
    name[0] = '\0';
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    manager = Global_R9 + 0x9588;
    uc_mem_read(MTK, manager + 4, &current, 4);
    uc_mem_read(MTK, manager + 8, &managerCount, 2);
    if (current)
    {
        uc_mem_read(MTK, current, &recordType, 2);
        uc_mem_read(MTK, current + 2, nameBytes, sizeof(nameBytes));
        uc_mem_read(MTK, current + 0x24, &currentActorName, 4);
        uc_mem_read(MTK, current + 0x28, &vargR3, 4);
        uc_mem_read(MTK, current + 0x2C, &n19202288, 4);
        uc_mem_read(MTK, current + 0x30, &callbackChoice, 1);
        uc_mem_read(MTK, current + 0x3C, &contextA, 4);
        uc_mem_read(MTK, current + 0x40, &contextB, 4);
        vm_format_trace_bytes_hex(nameBytes, (u32)sizeof(nameBytes), nameHex, sizeof(nameHex));
        vm_net_read_guest_best_effort_label(current + 2, name, sizeof(name));
    }

    ++s_managerReplayTraceCount;
    vm_net_trace("trace_manager_replay_entry pc=%08x lr=%08x last=%08x tick=%u manager=%08x managerCount=%u current=%08x recordType=%u name=%s nameHex=%s currentActorName=%08x vargR3=%08x n19202288=%08x callbackChoice=%u context=%08x,%08x activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 manager,
                 (unsigned int)managerCount,
                 current,
                 (unsigned int)recordType,
                 name,
                 nameHex,
                 currentActorName,
                 vargR3,
                 n19202288,
                 (unsigned int)callbackChoice,
                 contextA,
                 contextB,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_managerReplayTraceCount);
}

static void vm_trace_scene_message_request(const char *label, u32 pc)
{
    static u32 s_sceneMessageTraceCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 stack0 = 0;
    u32 stack4 = 0;
    u32 stack8 = 0;
    u32 stack12 = 0;
    u32 messageQueueMode = 0;
    u32 messageActive = 0;
    u32 pendingMessageCount = 0;
    u8 sceneGateA = 0;
    u8 sceneGateB = 0;
    u8 globalGate = 0;
    u8 widgetFlag = 0;
    u16 widgetFrame = 0;
    int16_t managerCount = 0;
    u8 textBytes[32] = {0};
    char text[128];
    char textHex[128];

    if (s_sceneMessageTraceCount >= 48 || !Global_R9)
        return;

    text[0] = '\0';
    textHex[0] = '\0';
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    if (sp)
    {
        (void)uc_mem_read(MTK, sp, &stack0, 4);
        (void)uc_mem_read(MTK, sp + 4, &stack4, 4);
        (void)uc_mem_read(MTK, sp + 8, &stack8, 4);
        (void)uc_mem_read(MTK, sp + 12, &stack12, 4);
    }
    if (r0)
    {
        (void)uc_mem_read(MTK, r0, textBytes, sizeof(textBytes));
        vm_format_trace_bytes_hex(textBytes, (u32)sizeof(textBytes), textHex, sizeof(textHex));
        vm_net_read_guest_best_effort_label(r0, text, sizeof(text));
    }

    (void)uc_mem_read(MTK, Global_R9 + 0x5D44, &messageQueueMode, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x5D80, &messageActive, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x5D84, &pendingMessageCount, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x5C67, &sceneGateA, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x5C68, &sceneGateB, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x5530, &globalGate, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x60F4 + 0x10, &widgetFlag, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x60F4 + 0x0A, &widgetFrame, 2);
    (void)uc_mem_read(MTK, Global_R9 + 0x9590, &managerCount, 2);

    ++s_sceneMessageTraceCount;
    vm_net_trace("trace_scene_message_request label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " textPtr=%08x text=%s textHex=%s cb1=%08x cb2=%08x arg3=%08x stack=%08x,%08x,%08x,%08x"
                 " messageQueueMode=%u messageActive=%u pendingMessage=%u sceneGate=%u,%u gate5530=%u widgetFlag=%u widgetFrame=%u managerCount=%d"
                 " activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 text,
                 textHex,
                 r1,
                 r2,
                 r3,
                 stack0,
                 stack4,
                 stack8,
                 stack12,
                 messageQueueMode,
                 messageActive,
                 pendingMessageCount,
                 sceneGateA,
                 sceneGateB,
                 globalGate,
                 widgetFlag,
                 widgetFrame,
                 managerCount,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneMessageTraceCount);
}

static void vm_trace_scene_loading_callback_gate(const char *label, u32 pc)
{
    static u32 s_sceneLoadingCallbackTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 waitObj = 0;
    u32 waitObjFlag10 = 0;
    u32 waitCallback = 0;
    u32 loadingState = 0;
    u8 gate5530 = 0;
    u8 gate5531 = 0;
    u8 sceneGateA = 0;
    u8 sceneGateB = 0;
    u8 widgetFlag = 0;
    u16 widgetFrame = 0;
    int16_t managerCount = 0;

    if (s_sceneLoadingCallbackTraceCount >= 160 || !Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    (void)uc_mem_read(MTK, Global_R9 + 0x5540, &waitObj, 4);
    if (waitObj)
        (void)uc_mem_read(MTK, waitObj + 0x10, &waitObjFlag10, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x554C, &waitCallback, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x552C, &loadingState, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x5530, &gate5530, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x5531, &gate5531, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x5C67, &sceneGateA, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x5C68, &sceneGateB, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x60F4 + 0x10, &widgetFlag, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x60F4 + 0x0A, &widgetFrame, 2);
    (void)uc_mem_read(MTK, Global_R9 + 0x9590, &managerCount, 2);

    if (pc == 0x1013BDC && waitObjFlag10 == 0 && gate5530 == 0 && gate5531 == 0 && widgetFrame < 280)
        return;

    ++s_sceneLoadingCallbackTraceCount;
    vm_net_trace("trace_scene_loading_callback_gate label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " r0=%08x waitObj=%08x waitObjFlag10=%u waitCallback=%08x loadingState=%u gate5530=%u gate5531=%u"
                 " sceneGate=%u,%u widgetFlag=%u widgetFrame=%u managerCount=%d activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 waitObj,
                 waitObjFlag10,
                 waitCallback,
                 loadingState,
                 gate5530,
                 gate5531,
                 sceneGateA,
                 sceneGateB,
                 widgetFlag,
                 widgetFrame,
                 managerCount,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneLoadingCallbackTraceCount);
}

static void vm_trace_alloc_outgoing_game_event(const char *label, u32 pc)
{
    static u32 s_allocOutgoingTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 eventObj = 0;
    u32 eventCount = 0;
    u32 eventCap = 0;
    u32 eventBase = 0;
    u8 eventByte5 = 0;

    if (s_allocOutgoingTraceCount >= 160 || !Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);

    (void)uc_mem_read(MTK, Global_R9 + 0x5540, &eventObj, 4);
    if (eventObj)
    {
        (void)uc_mem_read(MTK, eventObj + 0x05, &eventByte5, 1);
        (void)uc_mem_read(MTK, eventObj + 0x10, &eventCount, 4);
        (void)uc_mem_read(MTK, eventObj + 0x14, &eventCap, 4);
        (void)uc_mem_read(MTK, eventObj + 0x18, &eventBase, 4);
    }

    ++s_allocOutgoingTraceCount;
    vm_net_trace("trace_alloc_outgoing_game_event label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x r4=%08x r5=%08x eventObj=%08x byte5=%u count=%u cap=%u base=%08x"
                 " activeScreen=%08x currentThis=%08x countTrace=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 eventObj,
                 eventByte5,
                 eventCount,
                 eventCap,
                 eventBase,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_allocOutgoingTraceCount);
}

static void vm_net_trace_battle_module_state(const char *label, u32 pc)
{
    static u32 s_battleModuleTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r9 = 0;
    u32 battleBase = 0;
    u32 mainObj = 0;
    u32 enemyTable = 0;
    u32 side = 0;
    u16 ownCount = 0;
    u16 enemyCount = 0;
    u16 subtype = 0;
    u16 parseOk = 0;
    u8 pendingNet = 0;
    u8 battleFlag = 0;
    u8 autoRevive = 0;
    u8 localState = 0;
    u8 mainBattleType = 0;
    u8 mainBattleMode = 0;
    u8 mainBattleFlagA = 0;
    u8 mainBattleFlagB = 0;
    u8 mainBusy1206 = 0;
    u8 mainBusy1207 = 0;
    u32 enemy0 = 0;
    u32 enemy1 = 0;
    u32 enemy2 = 0;
    u32 enemy3 = 0;
    u32 ownHead[8] = {0};
    u32 enemyHead[8] = {0};

    if (s_battleModuleTraceCount >= 160)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    battleBase = r9 + 0x3450u;
    (void)uc_mem_read(MTK, battleBase + 0x60, &side, 4);
    (void)uc_mem_read(MTK, battleBase + 0x1A, &ownCount, 2);
    (void)uc_mem_read(MTK, battleBase + 0x1C, &enemyCount, 2);
    (void)uc_mem_read(MTK, battleBase + 0x20, &subtype, 2);
    (void)uc_mem_read(MTK, battleBase + 0x22, &parseOk, 2);
    (void)uc_mem_read(MTK, battleBase + 0x50, &enemyTable, 4);
    (void)uc_mem_read(MTK, r9 + 0x345Au, &battleFlag, 1);
    (void)uc_mem_read(MTK, r9 + 0x345Bu, &autoRevive, 1);
    (void)uc_mem_read(MTK, r9 + 0x345Eu, &localState, 1);
    (void)uc_mem_read(MTK, r9 + 0x4058u, &pendingNet, 1);
    (void)uc_mem_read(MTK, r9 + 0x2050u, &mainObj, 4);
    (void)uc_mem_read(MTK, r9 + 0x374Cu, ownHead, sizeof(ownHead));
    (void)uc_mem_read(MTK, r9 + 0x374Cu + 0xC4u, enemyHead, sizeof(enemyHead));
    if (enemyTable)
    {
        (void)uc_mem_read(MTK, enemyTable + 0x24u, &enemy0, 4);
        (void)uc_mem_read(MTK, enemyTable + 0x24u + 0x4Cu, &enemy1, 4);
        (void)uc_mem_read(MTK, enemyTable + 0x24u + 0x98u, &enemy2, 4);
        (void)uc_mem_read(MTK, enemyTable + 0x24u + 0xE4u, &enemy3, 4);
    }
    if (mainObj)
    {
        (void)uc_mem_read(MTK, mainObj + 1136, &mainBattleType, 1);
        (void)uc_mem_read(MTK, mainObj + 1138, &mainBattleMode, 1);
        (void)uc_mem_read(MTK, mainObj + 1139, &mainBattleFlagA, 1);
        (void)uc_mem_read(MTK, mainObj + 1140, &mainBattleFlagB, 1);
        (void)uc_mem_read(MTK, mainObj + 1206, &mainBusy1206, 1);
        (void)uc_mem_read(MTK, mainObj + 1207, &mainBusy1207, 1);
    }

    ++s_battleModuleTraceCount;
    vm_net_trace("trace_battle_module_state label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " r9=%08x regs=%08x,%08x,%08x,%08x side=%u ownCount=%u enemyCount=%u subtype=%u parseOk=%u"
                 " battleFlag=%u autoRevive=%u localState=%u pendingNet=%u mainObj=%08x mainBattle=%u,%u,%u,%u busy=%u,%u"
                 " enemyTable=%08x enemyIds=%u,%u,%u,%u"
                 " ownHead=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x"
                 " enemyHead=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x"
                 " activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r9,
                 r0,
                 r1,
                 r2,
                 r3,
                 side,
                 (unsigned int)ownCount,
                 (unsigned int)enemyCount,
                 (unsigned int)subtype,
                 (unsigned int)parseOk,
                 (unsigned int)battleFlag,
                 (unsigned int)autoRevive,
                 (unsigned int)localState,
                 (unsigned int)pendingNet,
                 mainObj,
                 (unsigned int)mainBattleType,
                 (unsigned int)mainBattleMode,
                 (unsigned int)mainBattleFlagA,
                 (unsigned int)mainBattleFlagB,
                 (unsigned int)mainBusy1206,
                 (unsigned int)mainBusy1207,
                 enemyTable,
                 enemy0,
                 enemy1,
                 enemy2,
                 enemy3,
                 ownHead[0],
                 ownHead[1],
                 ownHead[2],
                 ownHead[3],
                 ownHead[4],
                 ownHead[5],
                 ownHead[6],
                 ownHead[7],
                 enemyHead[0],
                 enemyHead[1],
                 enemyHead[2],
                 enemyHead[3],
                 enemyHead[4],
                 enemyHead[5],
                 enemyHead[6],
                 enemyHead[7],
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_battleModuleTraceCount);
}

static bool vm_net_trace_read_u32(u32 addr, u32 *value)
{
    if (value == NULL)
        return false;
    *value = 0;
    if (addr == 0)
        return false;
    return uc_mem_read(MTK, addr, value, 4) == UC_ERR_OK;
}

static bool vm_net_trace_read_u16(u32 addr, u16 *value)
{
    if (value == NULL)
        return false;
    *value = 0;
    if (addr == 0)
        return false;
    return uc_mem_read(MTK, addr, value, 2) == UC_ERR_OK;
}

static bool vm_net_trace_read_u8(u32 addr, u8 *value)
{
    if (value == NULL)
        return false;
    *value = 0;
    if (addr == 0)
        return false;
    return uc_mem_read(MTK, addr, value, 1) == UC_ERR_OK;
}

void vm_net_trace_battle_local_state_write(uint64_t address, uint32_t size, int64_t value)
{
    static u32 s_battleLocalStateWriteTraceCount = 0;
    u32 writeAddr = (u32)address;
    u32 writeEnd = writeAddr + size;
    u32 pc = 0, lr = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 localBase = 0;
    u32 battleStateBase = 0;
    u32 fighterBase = 0;
    u32 fighterTableBytes = 0;
    u32 activeSlot = 0;
    u32 activeBlock = 0;
    u32 activeActionBlock = 0;
    u32 pendingAddr = 0;
    u32 oldValue = 0;
    u16 ownCount = 0, enemyCount = 0, subtype = 0, parseOk = 0;
    u16 pendingA = 0, pendingB = 0;
    u8 stateBytes[16];
    u32 ownHp = 0, ownMaxHp = 0, enemyHp = 0, enemyMaxHp = 0;
    const char *label = "battle_local_misc";
    bool overlaps = false;

    if (s_battleLocalStateWriteTraceCount >= 2048 || size == 0)
        return;

    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    if (r9 == 0)
        return;

    localBase = r9 + 0x2918u;
    battleStateBase = r9 + 0x3450u;
    pendingAddr = r9 + 0x34B4u;
    fighterBase = r9 + 0x374Cu;
    (void)vm_net_trace_read_u16(battleStateBase + 0x1Au, &ownCount);
    (void)vm_net_trace_read_u16(battleStateBase + 0x1Cu, &enemyCount);
    (void)vm_net_trace_read_u16(battleStateBase + 0x20u, &subtype);
    (void)vm_net_trace_read_u16(battleStateBase + 0x22u, &parseOk);
    if (parseOk == 0 || ownCount > 6 || enemyCount > 6 || (ownCount + enemyCount) == 0)
        return;
    fighterTableBytes = (u32)(ownCount + enemyCount) * 0xC4u;
    overlaps = (writeAddr < localBase + 0xC20u && writeEnd > localBase) ||
               (writeAddr < pendingAddr + 4u && writeEnd > pendingAddr) ||
               (writeAddr < fighterBase + fighterTableBytes && writeEnd > fighterBase);
    if (!overlaps)
        return;

    memset(stateBytes, 0, sizeof(stateBytes));
    (void)vm_net_trace_read_u8(localBase + 0x02u, (u8 *)&activeSlot);
    activeBlock = localBase + (u32)(activeSlot & 0xffu) * 0xC4u;
    activeActionBlock = activeBlock + 0x520u;
    if (size == 1)
        (void)vm_net_trace_read_u8(writeAddr, (u8 *)&oldValue);
    else if (size == 2)
        (void)vm_net_trace_read_u16(writeAddr, (u16 *)&oldValue);
    else
        (void)vm_net_trace_read_u32(writeAddr, &oldValue);
    (void)uc_mem_read(MTK, localBase, stateBytes, sizeof(stateBytes));
    (void)vm_net_trace_read_u16(pendingAddr, &pendingA);
    (void)vm_net_trace_read_u16(pendingAddr + 2u, &pendingB);
    (void)vm_net_trace_read_u32(fighterBase + 0x10u, &ownHp);
    (void)vm_net_trace_read_u32(fighterBase + 0x14u, &ownMaxHp);
    (void)vm_net_trace_read_u32(fighterBase + 0xC4u + 0x10u, &enemyHp);
    (void)vm_net_trace_read_u32(fighterBase + 0xC4u + 0x14u, &enemyMaxHp);

    if (writeAddr < pendingAddr + 4u && writeEnd > pendingAddr)
        label = "pending_delta";
    else if (writeAddr < fighterBase + 0xC4u && writeEnd > fighterBase)
        label = "own_fighter_table";
    else if (writeAddr < fighterBase + 0x188u && writeEnd > fighterBase + 0xC4u)
        label = "enemy_fighter_table";
    else if (writeAddr < fighterBase + fighterTableBytes && writeEnd > fighterBase)
        label = "fighter_table_extra";
    else if (writeAddr < localBase + 0x10u && writeEnd > localBase)
        label = "local_state_header";
    else if (writeAddr < activeActionBlock + 0x64u && writeEnd > activeActionBlock)
        label = "active_action_block";
    else if (writeAddr < activeBlock + 0xC4u && writeEnd > activeBlock)
        label = "active_fighter_block";
    else if (writeAddr < localBase + 0x188u && writeEnd > localBase + 0xC4u)
        label = "enemy_fighter_block";

    ++s_battleLocalStateWriteTraceCount;
    vm_net_trace("trace_battle_local_state_write label=%s writeAddr=%08x writeSize=%u old=%08x raw=%08x"
                 " pc=%08x lr=%08x caller=%08x last=%08x tick=%u r9=%08x localBase=%08x battle=%u,%u,%u,%u fighterBase=%08x activeSlot=%u"
                 " activeBlock=%08x activeActionBlock=%08x state=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u"
                 " pendingDelta=%d,%d ownHp=%u/%u enemyHp=%u/%u regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x count=%u"
                 " evidence=runtime:Battle.cbm_local_state_memory_write_watch_for_actioninfo_hp_ko_delta_gated_by_4_10_parse_state\n",
                 label,
                 writeAddr,
                 size,
                 oldValue,
                 (u32)value,
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r9,
                 localBase,
                 (unsigned int)ownCount,
                 (unsigned int)enemyCount,
                 (unsigned int)subtype,
                 (unsigned int)parseOk,
                 fighterBase,
                 (unsigned int)(activeSlot & 0xffu),
                 activeBlock,
                 activeActionBlock,
                 (unsigned int)stateBytes[0],
                 (unsigned int)stateBytes[1],
                 (unsigned int)stateBytes[2],
                 (unsigned int)stateBytes[3],
                 (unsigned int)stateBytes[4],
                 (unsigned int)stateBytes[5],
                 (unsigned int)stateBytes[6],
                 (unsigned int)stateBytes[7],
                 (unsigned int)stateBytes[8],
                 (unsigned int)stateBytes[9],
                 (unsigned int)stateBytes[10],
                 (unsigned int)stateBytes[11],
                 (unsigned int)stateBytes[12],
                 (unsigned int)stateBytes[13],
                 (unsigned int)stateBytes[14],
                 (unsigned int)stateBytes[15],
                 (int)(short)pendingA,
                 (int)(short)pendingB,
                 ownHp,
                 ownMaxHp,
                 enemyHp,
                 enemyMaxHp,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 s_battleLocalStateWriteTraceCount);
}

static void vm_net_trace_battle_create_charlist_source(const char *label, u32 pc, u32 moduleBase)
{
    static u32 s_battleCreateCharListTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 battleBase = 0;
    u32 mainObj = 0;
    u32 direct0 = 0, direct4 = 0, direct8 = 0, directC = 0, direct10 = 0, direct14 = 0, direct28 = 0, direct40 = 0;
    u32 ext1C = 0, ext20 = 0, ext28 = 0, ext2C = 0, ext30 = 0;
    u32 table24 = 0, table28 = 0, table2C = 0, table4C = 0, compactTable = 0, fullTable = 0, table58 = 0;
    u32 retIds[4] = {0};
    u32 curIds[4] = {0};
    u32 retRaw0[4] = {0};
    u32 retRaw44[4] = {0};
    u32 curRaw0[4] = {0};
    u32 curRaw44[4] = {0};
    u32 fullIds[6] = {0};
    u32 fullId24[6] = {0};
    u32 fullId48[6] = {0};
    u16 fullPosX[6] = {0};
    u16 fullPosY[6] = {0};
    u8 fullFlag13B[6] = {0};
    u8 fullFlag13F[6] = {0};
    u8 fullFlag147[6] = {0};
    u32 globalSceneTable = 0;
    u32 globalCompactTable = 0;
    u32 globalCurrentNode = 0;
    u32 globalActorId = 0;
    u32 off = moduleBase && pc >= moduleBase ? pc - moduleBase : 0xffffffffu;

    if (s_battleCreateCharListTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    if (r9)
    {
        battleBase = r9 + 0x3450u;
        (void)vm_net_trace_read_u32(r9 + 0x2050u, &mainObj);
        (void)vm_net_trace_read_u32(battleBase + 0x24u, &table24);
        (void)vm_net_trace_read_u32(battleBase + 0x28u, &table28);
        (void)vm_net_trace_read_u32(battleBase + 0x2Cu, &table2C);
        (void)vm_net_trace_read_u32(battleBase + 0x4Cu, &table4C);
        (void)vm_net_trace_read_u32(battleBase + 0x50u, &compactTable);
        (void)vm_net_trace_read_u32(battleBase + 0x54u, &fullTable);
        (void)vm_net_trace_read_u32(battleBase + 0x58u, &table58);
    }
    if (mainObj)
    {
        (void)vm_net_trace_read_u32(mainObj + 0x00u, &direct0);
        (void)vm_net_trace_read_u32(mainObj + 0x04u, &direct4);
        (void)vm_net_trace_read_u32(mainObj + 0x08u, &direct8);
        (void)vm_net_trace_read_u32(mainObj + 0x0Cu, &directC);
        (void)vm_net_trace_read_u32(mainObj + 0x10u, &direct10);
        (void)vm_net_trace_read_u32(mainObj + 0x14u, &direct14);
        (void)vm_net_trace_read_u32(mainObj + 0x28u, &direct28);
        (void)vm_net_trace_read_u32(mainObj + 0x40u, &direct40);
        (void)vm_net_trace_read_u32(mainObj + 0x11Cu, &ext1C);
        (void)vm_net_trace_read_u32(mainObj + 0x120u, &ext20);
        (void)vm_net_trace_read_u32(mainObj + 0x128u, &ext28);
        (void)vm_net_trace_read_u32(mainObj + 0x12Cu, &ext2C);
        (void)vm_net_trace_read_u32(mainObj + 0x130u, &ext30);
    }
    if (compactTable)
    {
        for (u32 i = 0; i < 4; ++i)
        {
            u32 rec = compactTable + i * 0x4Cu;
            (void)vm_net_trace_read_u32(rec + 0x00u, &curRaw0[i]);
            (void)vm_net_trace_read_u32(rec + 0x24u, &curIds[i]);
            (void)vm_net_trace_read_u32(rec + 0x44u, &curRaw44[i]);
        }
    }
    if (r0)
    {
        for (u32 i = 0; i < 4; ++i)
        {
            u32 rec = r0 + i * 0x4Cu;
            (void)vm_net_trace_read_u32(rec + 0x00u, &retRaw0[i]);
            (void)vm_net_trace_read_u32(rec + 0x24u, &retIds[i]);
            (void)vm_net_trace_read_u32(rec + 0x44u, &retRaw44[i]);
        }
    }
    if (fullTable)
    {
        for (u32 i = 0; i < 6; ++i)
        {
            u32 rec = fullTable + i * 0x154u;
            (void)vm_net_trace_read_u32(rec + 0x24u, &fullId24[i]);
            (void)vm_net_trace_read_u32(rec + 0x48u, &fullId48[i]);
            (void)vm_net_trace_read_u32(rec + 0x64u, &fullIds[i]);
            (void)vm_net_trace_read_u16(rec + 0x30u, &fullPosX[i]);
            (void)vm_net_trace_read_u16(rec + 0x34u, &fullPosY[i]);
            (void)vm_net_trace_read_u8(rec + 0x13Bu, &fullFlag13B[i]);
            (void)vm_net_trace_read_u8(rec + 0x13Fu, &fullFlag13F[i]);
            (void)vm_net_trace_read_u8(rec + 0x147u, &fullFlag147[i]);
        }
    }
    if (Global_R9)
    {
        (void)vm_net_trace_read_u32(Global_R9 + 23728u, &globalSceneTable);
        (void)vm_net_trace_read_u32(Global_R9 + 23796u, &globalCompactTable);
        (void)vm_net_trace_read_u32(Global_R9 + 23716u, &globalCurrentNode);
        if (globalCurrentNode)
            (void)vm_net_trace_read_u32(globalCurrentNode + 0x64u, &globalActorId);
    }

    ++s_battleCreateCharListTraceCount;
    vm_net_trace("trace_battle_create_charlist_source label=%s pc=%08x off=%04x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x battleBase=%08x"
                 " mainObj=%08x direct=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x ext=%08x,%08x,%08x,%08x,%08x"
                 " tables=%08x,%08x,%08x,%08x,%08x,%08x,%08x"
                 " retAsCompact=%08x ids=%u,%u,%u,%u raw0=%08x,%08x,%08x,%08x raw44=%08x,%08x,%08x,%08x"
                 " curCompact=%08x ids=%u,%u,%u,%u raw0=%08x,%08x,%08x,%08x raw44=%08x,%08x,%08x,%08x"
                 " fullTable=%08x ids64=%u,%u,%u,%u,%u,%u ids24=%u,%u,%u,%u,%u,%u ids48=%u,%u,%u,%u,%u,%u"
                 " pos=%u:%u,%u:%u,%u:%u,%u:%u,%u:%u,%u:%u flags13b=%u,%u,%u,%u,%u,%u flags13f=%u,%u,%u,%u,%u,%u flags147=%u,%u,%u,%u,%u,%u"
                 " global=%08x sceneTable=%08x compactTable=%08x currentNode=%08x currentActorId=%u activeScreen=%08x currentThis=%08x count=%u"
                 " evidence=Battle.cbm:BattleScene_CreateCharList_0x6462_0x646A_0x65A8_0x65AE,sub_66A4_enemy_template_table\n",
                 label ? label : "unknown",
                 pc, off, lr, lr & ~1u, lastAddress, g_schedulerTick,
                 r0, r1, r2, r3, r4, r5, r6, r7, sp, r9, battleBase,
                 mainObj, direct0, direct4, direct8, directC, direct10, direct14, direct28, direct40,
                 ext1C, ext20, ext28, ext2C, ext30,
                 table24, table28, table2C, table4C, compactTable, fullTable, table58,
                 r0, retIds[0], retIds[1], retIds[2], retIds[3],
                 retRaw0[0], retRaw0[1], retRaw0[2], retRaw0[3],
                 retRaw44[0], retRaw44[1], retRaw44[2], retRaw44[3],
                 compactTable, curIds[0], curIds[1], curIds[2], curIds[3],
                 curRaw0[0], curRaw0[1], curRaw0[2], curRaw0[3],
                 curRaw44[0], curRaw44[1], curRaw44[2], curRaw44[3],
                 fullTable,
                 fullIds[0], fullIds[1], fullIds[2], fullIds[3], fullIds[4], fullIds[5],
                 fullId24[0], fullId24[1], fullId24[2], fullId24[3], fullId24[4], fullId24[5],
                 fullId48[0], fullId48[1], fullId48[2], fullId48[3], fullId48[4], fullId48[5],
                 (unsigned int)fullPosX[0], (unsigned int)fullPosY[0],
                 (unsigned int)fullPosX[1], (unsigned int)fullPosY[1],
                 (unsigned int)fullPosX[2], (unsigned int)fullPosY[2],
                 (unsigned int)fullPosX[3], (unsigned int)fullPosY[3],
                 (unsigned int)fullPosX[4], (unsigned int)fullPosY[4],
                 (unsigned int)fullPosX[5], (unsigned int)fullPosY[5],
                 (unsigned int)fullFlag13B[0], (unsigned int)fullFlag13B[1], (unsigned int)fullFlag13B[2],
                 (unsigned int)fullFlag13B[3], (unsigned int)fullFlag13B[4], (unsigned int)fullFlag13B[5],
                 (unsigned int)fullFlag13F[0], (unsigned int)fullFlag13F[1], (unsigned int)fullFlag13F[2],
                 (unsigned int)fullFlag13F[3], (unsigned int)fullFlag13F[4], (unsigned int)fullFlag13F[5],
                 (unsigned int)fullFlag147[0], (unsigned int)fullFlag147[1], (unsigned int)fullFlag147[2],
                 (unsigned int)fullFlag147[3], (unsigned int)fullFlag147[4], (unsigned int)fullFlag147[5],
                 Global_R9, globalSceneTable, globalCompactTable, globalCurrentNode, globalActorId,
                 vmAddedScreen, g_currentScreenThis, s_battleCreateCharListTraceCount);
}

static void vm_net_trace_battle_outgoing_request_source(const char *label, u32 pc, u32 moduleBase)
{
    static u32 s_battleOutgoingSourceTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 battleBase = 0;
    u32 enemyTable = 0;
    u32 enemyIds[4] = {0};
    u16 ownCount = 0, enemyCount = 0, subtype = 0, parseOk = 0;

    if (s_battleOutgoingSourceTraceCount >= 80)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    battleBase = r9 + 0x3450u;
    (void)vm_net_trace_read_u16(battleBase + 0x1Au, &ownCount);
    (void)vm_net_trace_read_u16(battleBase + 0x1Cu, &enemyCount);
    (void)vm_net_trace_read_u16(battleBase + 0x20u, &subtype);
    (void)vm_net_trace_read_u16(battleBase + 0x22u, &parseOk);
    (void)vm_net_trace_read_u32(battleBase + 0x50u, &enemyTable);
    if (enemyTable)
    {
        for (u32 i = 0; i < 4; ++i)
            (void)vm_net_trace_read_u32(enemyTable + i * 0x4Cu + 0x24u, &enemyIds[i]);
    }

    ++s_battleOutgoingSourceTraceCount;
    vm_net_trace("trace_battle_outgoing_request_source label=%s pc=%08x off=%04x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " counts=%u,%u subtype=%u parseOk=%u enemyTable=%08x enemyIds=%u,%u,%u,%u"
                 " activeScreen=%08x currentThis=%08x count=%u evidence=Battle.cbm:outgoing_request_builder_entry\n",
                 label ? label : "unknown", pc, moduleBase ? (pc - moduleBase) : 0, lr, lr & ~1u,
                 lastAddress, g_schedulerTick, r0, r1, r2, r3, r4, r5, r6, r7, sp, r9,
                 (unsigned int)ownCount, (unsigned int)enemyCount, (unsigned int)subtype, (unsigned int)parseOk,
                 enemyTable, enemyIds[0], enemyIds[1], enemyIds[2], enemyIds[3],
                 vmAddedScreen, g_currentScreenThis, s_battleOutgoingSourceTraceCount);
}

static void vm_net_trace_battle_challenge_source_branch(const char *label, u32 pc, u32 moduleBase)
{
    static u32 s_battleChallengeBranchTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 globalObj = 0;
    u32 method6c = 0;
    u32 stack90 = 0;
    u32 stack8c = 0;
    u32 directWord0 = 0;
    u32 directWord1 = 0;
    u16 directId = 0;
    u16 directIndex = 0;
    u8 sentFlag = 0;
    u8 gateFlag9 = 0;
    u8 gateFlag10 = 0;
    u32 fallbackRecord = 0;
    u32 fallbackId24 = 0;
    u32 fallbackId48 = 0;
    u32 fallbackPosX = 0;
    u32 fallbackPosY = 0;
    u32 battleBase = 0;
    u32 enemyTable = 0;
    u32 enemyIds[4] = {0};

    if (s_battleChallengeBranchTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    (void)vm_net_trace_read_u32(r6, &globalObj);
    if (globalObj)
    {
        (void)vm_net_trace_read_u32(globalObj + 0x6Cu, &method6c);
        (void)vm_net_trace_read_u16(globalObj + 0x3B4u, &directId);
        (void)vm_net_trace_read_u16(globalObj + 0x3B6u, &directIndex);
        (void)vm_net_trace_read_u32(globalObj + 0x3B4u, &directWord0);
        (void)vm_net_trace_read_u32(globalObj + 0x3B8u, &directWord1);
        (void)vm_net_trace_read_u8(globalObj + r7, &sentFlag);
        (void)vm_net_trace_read_u8(globalObj + 0x399u, &gateFlag9);
        (void)vm_net_trace_read_u8(globalObj + 0x39Au, &gateFlag10);
    }
    (void)vm_net_trace_read_u32(sp + 0x90u, &stack90);
    (void)vm_net_trace_read_u32(sp + 0x8Cu, &stack8c);
    if (stack8c < 16)
    {
        fallbackRecord = r6 + 0xC0u + stack8c * 0x154u;
        (void)vm_net_trace_read_u32(fallbackRecord + 0x24u, &fallbackId24);
        (void)vm_net_trace_read_u32(fallbackRecord + 0x48u, &fallbackId48);
        (void)vm_net_trace_read_u32(fallbackRecord + 0x30u, &fallbackPosX);
        (void)vm_net_trace_read_u32(fallbackRecord + 0x34u, &fallbackPosY);
    }

    battleBase = r9 + 0x3450u;
    (void)vm_net_trace_read_u32(battleBase + 0x50u, &enemyTable);
    if (enemyTable)
    {
        for (u32 i = 0; i < 4; ++i)
            (void)vm_net_trace_read_u32(enemyTable + i * 0x4Cu + 0x24u, &enemyIds[i]);
    }

    ++s_battleChallengeBranchTraceCount;
    vm_net_trace("trace_battle_challenge_source_branch label=%s pc=%08x off=%04x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " global=%08x method6c=%08x stack90=%u stack8c=%u direct=%u,%u directWords=%08x,%08x"
                 " flags=%u,%u,%u record=%08x recIds=%u,%u recPos=%u,%u enemyTable=%08x enemyIds=%u,%u,%u,%u"
                 " activeScreen=%08x currentThis=%08x count=%u evidence=Battle.cbm:0x05182940_fallback_challenge_source\n",
                 label ? label : "unknown", pc, moduleBase ? (pc - moduleBase) : 0, lr, lr & ~1u,
                 lastAddress, g_schedulerTick, r0, r1, r2, r3, r4, r5, r6, r7, sp, r9,
                 globalObj, method6c, stack90, stack8c, directId, directIndex, directWord0, directWord1,
                 (unsigned int)sentFlag, (unsigned int)gateFlag9, (unsigned int)gateFlag10,
                 fallbackRecord, fallbackId24, fallbackId48, fallbackPosX, fallbackPosY,
                 enemyTable, enemyIds[0], enemyIds[1], enemyIds[2], enemyIds[3],
                 vmAddedScreen, g_currentScreenThis, s_battleChallengeBranchTraceCount);
}

static void vm_net_trace_battle_pool_probe(const char *label, u32 pc, u32 moduleBase)
{
    static u32 s_battlePoolProbeCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 r6 = 0;
    u32 r7 = 0;
    u32 r9 = 0;
    u32 off = moduleBase && pc >= moduleBase ? pc - moduleBase : 0xffffffffu;
    u32 objA = 0;
    u32 objB = 0;
    u32 objAKind = 0;
    u32 objASubtype = 0;
    u32 objBKind = 0;
    u32 objBSubtype = 0;
    u32 objARawGet = 0;
    u32 objAStringGet = 0;
    u32 objAU32Get = 0;
    u32 objAU8Get = 0;
    u32 objALenGet = 0;
    u32 objBRawGet = 0;
    u32 objBStringGet = 0;
    u32 objBU32Get = 0;
    u32 objBU8Get = 0;
    u32 objBLenGet = 0;
    u32 streamMgr = 0;
    u32 streamInit = 0;
    u32 streamBase = 0;
    u32 streamU32 = 0;
    u32 streamU8 = 0;
    u32 streamString = 0;
    u32 streamSkip = 0;
    u32 bridgeObj = 0;
    u32 bridgeCb14 = 0;
    u32 bridgeCb18 = 0;
    u32 drawObj = 0;
    u32 drawCb24 = 0;
    u32 itemnum = 0;
    u32 loopIndex = 0;
    u32 sp65c = 0;
    u32 sp680 = 0;
    u32 sp72c = 0;
    u32 sp08 = 0;
    u32 sp0c = 0;
    u32 sp24 = 0;
    u32 sp3c = 0;
    u32 sp40 = 0;
    u32 sp14 = 0;
    u32 sp160 = 0;
    u32 sp170 = 0;
    u32 infoPtr = 0;
    u32 infoLen = 0;
    u8 infoBytes[16] = {0};
    u32 infoDumpLen = 0;
    u32 r9_2038 = 0;
    u32 r9_2044 = 0;
    u32 r9_2048 = 0;
    u32 r9_204c = 0;
    u32 r9_2080 = 0;
    u8 sp72cByteA = 0;
    u32 battleStruct = 0;
    u16 ownCount = 0;
    u16 enemyCount = 0;
    u16 subtype = 0;
    u16 parseOk = 0;
    u8 result = 0;
    u8 bagstatus = 0;
    u8 autorevive = 0;
    u32 hp = 0;
    u32 mp = 0;
    u32 own0[6] = {0};
    u32 enemy0[6] = {0};
    u32 fighterActive = 0;
    u32 fighterCb4 = 0;
    u32 fighterCb8 = 0;
    u32 fighterCbC = 0;
    u8 fighterFrameMul = 0;
    u8 fighterFrameSignedRaw = 0;
    u8 itemSlotIndex = 0;
    u32 itemSlotBase = 0;
    u32 itemSlot64 = 0;
    u32 itemSlot68 = 0;
    u32 itemSlot6c = 0;
    u8 itemSlot9e = 0;
    u8 itemSlot9f = 0;

    if (s_battlePoolProbeCount >= 1200)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    /*
     * Battle.cbm dispatchers pass the packet object in R1. sub_743C keeps it
     * in R6 after entry. Log both candidates; invalid pointers simply read as
     * zero and are treated as evidence, not patched.
     */
    objA = r1;
    objB = r6;
    (void)vm_net_trace_read_u32(objA + 4, &objAKind);
    (void)vm_net_trace_read_u32(objA + 8, &objASubtype);
    (void)vm_net_trace_read_u32(objA + 0x28, &objARawGet);
    (void)vm_net_trace_read_u32(objA + 0x40, &objAStringGet);
    (void)vm_net_trace_read_u32(objA + 0x44, &objAU32Get);
    (void)vm_net_trace_read_u32(objA + 0x4C, &objAU8Get);
    (void)vm_net_trace_read_u32(objA + 0x54, &objALenGet);
    (void)vm_net_trace_read_u32(objB + 4, &objBKind);
    (void)vm_net_trace_read_u32(objB + 8, &objBSubtype);
    (void)vm_net_trace_read_u32(objB + 0x28, &objBRawGet);
    (void)vm_net_trace_read_u32(objB + 0x40, &objBStringGet);
    (void)vm_net_trace_read_u32(objB + 0x44, &objBU32Get);
    (void)vm_net_trace_read_u32(objB + 0x4C, &objBU8Get);
    (void)vm_net_trace_read_u32(objB + 0x54, &objBLenGet);

    if (r9)
    {
        battleStruct = r9 + 0x3450u;
        (void)vm_net_trace_read_u32(r9 + 0x204Cu, &streamMgr);
        if (streamMgr)
        {
            (void)vm_net_trace_read_u32(streamMgr + 0x0C, &streamInit);
            (void)vm_net_trace_read_u32(streamMgr + 0x20, &streamU32);
            (void)vm_net_trace_read_u32(streamMgr + 0x28, &streamU8);
            (void)vm_net_trace_read_u32(streamMgr + 0x2C, &streamString);
            (void)vm_net_trace_read_u32(streamMgr + 0x30, &streamSkip);
        }
        (void)vm_net_trace_read_u32(r9 + 0x2018u, &bridgeObj);
        if (bridgeObj)
        {
            (void)vm_net_trace_read_u32(bridgeObj + 0x14u, &bridgeCb14);
            (void)vm_net_trace_read_u32(bridgeObj + 0x18u, &bridgeCb18);
        }
        (void)vm_net_trace_read_u32(r9 + 0x2040u, &drawObj);
        if (drawObj)
            (void)vm_net_trace_read_u32(drawObj + 0x24u, &drawCb24);
        (void)vm_net_trace_read_u32(r9 + 0x2038u, &r9_2038);
        (void)vm_net_trace_read_u32(r9 + 0x2044u, &r9_2044);
        (void)vm_net_trace_read_u32(r9 + 0x2048u, &r9_2048);
        (void)vm_net_trace_read_u32(r9 + 0x204Cu, &r9_204c);
        (void)vm_net_trace_read_u32(r9 + 0x2080u, &r9_2080);
        (void)vm_net_trace_read_u16(battleStruct + 0x1A, &ownCount);
        (void)vm_net_trace_read_u16(battleStruct + 0x1C, &enemyCount);
        (void)vm_net_trace_read_u16(battleStruct + 0x20, &subtype);
        (void)vm_net_trace_read_u16(battleStruct + 0x22, &parseOk);
        (void)vm_net_trace_read_u8(battleStruct + 0x0D, &result);
        (void)vm_net_trace_read_u8(battleStruct + 0x08, &bagstatus);
        (void)vm_net_trace_read_u8(battleStruct + 0x0B, &autorevive);
        (void)vm_net_trace_read_u32(battleStruct + 0x04, &hp);
        (void)vm_net_trace_read_u32(battleStruct + 0x00, &mp);
        (void)uc_mem_read(MTK, r9 + 0x374Cu, own0, sizeof(own0));
        (void)uc_mem_read(MTK, r9 + 0x374Cu + 0xC4u, enemy0, sizeof(enemy0));
    }
    /*
     * In the Battle.cbm render loop around actual-code-buffer offset 0x57D2,
     * R6 is the current 0xC4-byte fighter slot.  The loop checks
     * [R6+0x544] before calling callbacks stored at [R6+0x584]/[R6+0x588].
     */
    (void)vm_net_trace_read_u32(r6 + 0x544u, &fighterActive);
    (void)vm_net_trace_read_u32(r6 + 0x584u, &fighterCb4);
    (void)vm_net_trace_read_u32(r6 + 0x588u, &fighterCb8);
    (void)vm_net_trace_read_u32(r6 + 0x58Cu, &fighterCbC);
    (void)vm_net_trace_read_u8(r6 + 0x5C7u, &fighterFrameMul);
    (void)vm_net_trace_read_u8(r6 + 0x5C8u, &fighterFrameSignedRaw);

    (void)vm_net_trace_read_u32(sp + 0x08, &sp08);
    (void)vm_net_trace_read_u32(sp + 0x0C, &sp0c);
    (void)vm_net_trace_read_u32(sp + 0x14, &sp14);
    (void)vm_net_trace_read_u32(sp + 0x24, &sp24);
    (void)vm_net_trace_read_u32(sp + 0x3C, &sp3c);
    (void)vm_net_trace_read_u32(sp + 0x40, &sp40);
    (void)vm_net_trace_read_u32(sp + 0x160, &sp160);
    (void)vm_net_trace_read_u32(sp + 0x168, &itemnum);
    (void)vm_net_trace_read_u32(sp + 0x16C, &loopIndex);
    (void)vm_net_trace_read_u32(sp + 0x170, &sp170);
    (void)vm_net_trace_read_u32(sp + 0x65Cu, &sp65c);
    (void)vm_net_trace_read_u32(sp + 0x680u, &sp680);
    (void)vm_net_trace_read_u32(sp + 0x72Cu, &sp72c);
    if (sp72c)
        (void)vm_net_trace_read_u8(sp72c + 0x0Au, &sp72cByteA);
    if (sp170)
    {
        (void)vm_net_trace_read_u8(sp170 + 0x0Au, &itemSlotIndex);
        itemSlotBase = r9 + 0x2090u + ((u32)itemSlotIndex << 6);
        (void)vm_net_trace_read_u32(itemSlotBase + 0x64u, &itemSlot64);
        (void)vm_net_trace_read_u32(itemSlotBase + 0x68u, &itemSlot68);
        (void)vm_net_trace_read_u32(itemSlotBase + 0x6Cu, &itemSlot6c);
        (void)vm_net_trace_read_u8(itemSlotBase + 0x9Eu, &itemSlot9e);
        (void)vm_net_trace_read_u8(itemSlotBase + 0x9Fu, &itemSlot9f);
    }
    (void)vm_net_trace_read_u32(r4 + 0x400u, &streamBase);
    infoPtr = sp08;
    infoLen = r7;
    if (infoPtr != 0 && infoLen != 0)
    {
        infoDumpLen = infoLen < sizeof(infoBytes) ? infoLen : (u32)sizeof(infoBytes);
        if (uc_mem_read(MTK, infoPtr, infoBytes, infoDumpLen) != UC_ERR_OK)
            infoDumpLen = 0;
    }

    ++s_battlePoolProbeCount;
    vm_net_trace("trace_battle_pool_probe label=%s pc=%08x off=%04x lr=%08x caller=%08x last=%08x tick=%u"
                 " moduleBase=%08x regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x r9=%08x sp=%08x"
                 " objA=%08x kind=%u subtype=%u cbA=%08x,%08x,%08x,%08x,%08x"
                 " objB=%08x kind=%u subtype=%u cbB=%08x,%08x,%08x,%08x,%08x"
                 " streamMgr=%08x streamCbs=%08x,%08x,%08x,%08x,%08x streamBase=%08x"
                 " bridge=%08x cb14=%08x cb18=%08x draw=%08x draw24=%08x stack=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,b%u"
                 " info=%08x,%u,%02x%02x%02x%02x,%02x%02x%02x%02x,%02x%02x%02x%02x,%02x%02x%02x%02x"
                 " itemSlot=%u base=%08x vals=%08x,%08x,%08x flags=%u,%u"
                 " r9slots=%08x,%08x,%08x,%08x,%08x"
                 " fighterSlot=%08x active544=%08x cb584=%08x,%08x,%08x frame=%u,%d"
                 " counts=%u,%u subtypeState=%u parseOk=%u status=%u,%u,%u hpmp=%u,%u item=%u,%u"
                 " own0=%08x,%08x,%08x,%08x,%08x,%08x enemy0=%08x,%08x,%08x,%08x,%08x,%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 off,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 moduleBase,
                 r0, r1, r2, r3, r4, r5, r6, r7, r9, sp,
                 objA, objAKind, objASubtype, objARawGet, objAStringGet, objAU32Get, objAU8Get, objALenGet,
                 objB, objBKind, objBSubtype, objBRawGet, objBStringGet, objBU32Get, objBU8Get, objBLenGet,
                 streamMgr, streamInit, streamU32, streamU8, streamString, streamSkip, streamBase,
                 bridgeObj, bridgeCb14, bridgeCb18, drawObj, drawCb24,
                 sp08, sp0c, sp14, sp24, sp3c, sp40, sp160, sp65c, sp680, sp72c, (unsigned int)sp72cByteA,
                 infoPtr,
                 infoLen,
                 infoDumpLen > 0 ? infoBytes[0] : 0,
                 infoDumpLen > 1 ? infoBytes[1] : 0,
                 infoDumpLen > 2 ? infoBytes[2] : 0,
                 infoDumpLen > 3 ? infoBytes[3] : 0,
                 infoDumpLen > 4 ? infoBytes[4] : 0,
                 infoDumpLen > 5 ? infoBytes[5] : 0,
                 infoDumpLen > 6 ? infoBytes[6] : 0,
                 infoDumpLen > 7 ? infoBytes[7] : 0,
                 infoDumpLen > 8 ? infoBytes[8] : 0,
                 infoDumpLen > 9 ? infoBytes[9] : 0,
                 infoDumpLen > 10 ? infoBytes[10] : 0,
                 infoDumpLen > 11 ? infoBytes[11] : 0,
                 infoDumpLen > 12 ? infoBytes[12] : 0,
                 infoDumpLen > 13 ? infoBytes[13] : 0,
                 infoDumpLen > 14 ? infoBytes[14] : 0,
                 infoDumpLen > 15 ? infoBytes[15] : 0,
                 (unsigned int)itemSlotIndex,
                 itemSlotBase,
                 itemSlot64,
                 itemSlot68,
                 itemSlot6c,
                 (unsigned int)itemSlot9e,
                 (unsigned int)itemSlot9f,
                 r9_2038, r9_2044, r9_2048, r9_204c, r9_2080,
                 r6, fighterActive, fighterCb4, fighterCb8, fighterCbC,
                 (unsigned int)fighterFrameMul,
                 fighterFrameSignedRaw < 0x80 ? (int)fighterFrameSignedRaw : (int)fighterFrameSignedRaw - 0x100,
                 (unsigned int)ownCount,
                 (unsigned int)enemyCount,
                 (unsigned int)subtype,
                 (unsigned int)parseOk,
                 (unsigned int)result,
                 (unsigned int)bagstatus,
                 (unsigned int)autorevive,
                 hp,
                 mp,
                 itemnum,
                 loopIndex,
                 own0[0], own0[1], own0[2], own0[3], own0[4], own0[5],
                 enemy0[0], enemy0[1], enemy0[2], enemy0[3], enemy0[4], enemy0[5],
                 s_battlePoolProbeCount);
}

static void vm_net_trace_status_meter_rebuild_site(u32 pc)
{
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 statusMeterNode = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 n2 = 0;
    u32 sourceHead = 0;
    u32 currentPrimaryBaseMax = 0;
    u32 currentSecondaryBaseMax = 0;
    u32 currentPrimaryDisplayMax = 0;
    u32 currentSecondaryDisplayMax = 0;
    u32 meterPrimaryBaseMax = 0;
    u32 meterSecondaryBaseMax = 0;
    u32 meterPrimaryDisplayMax = 0;
    u32 meterSecondaryDisplayMax = 0;
    u32 pendingPrimaryDisplayMax = 0;
    u32 pendingSecondaryDisplayMax = 0;
    const char *label = "unknown";

    if (!Global_R9)
        return;

    hudState = Global_R9 + 0x5C64;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x48, &statusMeterNode, 4);
    uc_mem_read(MTK, Global_R9 + 0x6048, &sourceHead, 4);

    if (currentSceneNode)
    {
        uc_mem_read(MTK, currentSceneNode + 0xBC, &currentPrimaryBaseMax, 4);
        uc_mem_read(MTK, currentSceneNode + 0xC0, &currentSecondaryBaseMax, 4);
        uc_mem_read(MTK, currentSceneNode + 0xC4, &currentPrimaryDisplayMax, 4);
        uc_mem_read(MTK, currentSceneNode + 0xC8, &currentSecondaryDisplayMax, 4);
    }
    if (statusMeterNode)
    {
        uc_mem_read(MTK, statusMeterNode + 0xBC, &meterPrimaryBaseMax, 4);
        uc_mem_read(MTK, statusMeterNode + 0xC0, &meterSecondaryBaseMax, 4);
        uc_mem_read(MTK, statusMeterNode + 0xC4, &meterPrimaryDisplayMax, 4);
        uc_mem_read(MTK, statusMeterNode + 0xC8, &meterSecondaryDisplayMax, 4);
    }

    if (pc == 0x100FF26)
    {
        label = "seed_gate";
        uc_reg_read(MTK, UC_ARM_REG_R6, &n2);
    }
    else if (pc == 0x10100D2 || pc == 0x10101E8)
    {
        label = (pc == 0x10100D2) ? "writeback_early" : "writeback_common";
        if (sp)
        {
            uc_mem_read(MTK, sp + 0x70, &pendingPrimaryDisplayMax, 4);
            uc_mem_read(MTK, sp + 0x6C, &pendingSecondaryDisplayMax, 4);
        }
    }
    else
    {
        return;
    }

    vm_net_trace("trace_status_meter_rebuild_site label=%s pc=%08x lr=%08x last=%08x tick=%u n2=%u sourceHead=%08x currentNode=%08x meterNode=%08x currentBase=%u/%u currentDisplay=%u/%u meterBase=%u/%u meterDisplay=%u/%u pendingDisplay=%u/%u activeScreen=%08x currentThis=%08x\n",
                 label,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 n2,
                 sourceHead,
                 currentSceneNode,
                 statusMeterNode,
                 currentPrimaryBaseMax,
                 currentSecondaryBaseMax,
                 currentPrimaryDisplayMax,
                 currentSecondaryDisplayMax,
                 meterPrimaryBaseMax,
                 meterSecondaryBaseMax,
                 meterPrimaryDisplayMax,
                 meterSecondaryDisplayMax,
                 pendingPrimaryDisplayMax,
                 pendingSecondaryDisplayMax,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_scene_seed_status_meter_displaymax_fallback(u32 pc)
{
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 statusMeterNode = 0;
    u32 n2 = 0;
    u32 sourceHead = 0;
    u32 currentPrimaryBaseMax = 0;
    u32 currentSecondaryBaseMax = 0;
    u32 currentPrimaryDisplayMax = 0;
    u32 currentSecondaryDisplayMax = 0;
    u32 meterPrimaryDisplayMax = 0;
    u32 meterSecondaryDisplayMax = 0;

    if (pc != 0x100FF26 || !Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R6, &n2);
    if (n2 != 1)
        return;

    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x48, &statusMeterNode, 4);
    uc_mem_read(MTK, Global_R9 + 0x6048, &sourceHead, 4);

    if (!currentSceneNode || !statusMeterNode || sourceHead != 0)
        return;

    uc_mem_read(MTK, currentSceneNode + 0xBC, &currentPrimaryBaseMax, 4);
    uc_mem_read(MTK, currentSceneNode + 0xC0, &currentSecondaryBaseMax, 4);
    uc_mem_read(MTK, currentSceneNode + 0xC4, &currentPrimaryDisplayMax, 4);
    uc_mem_read(MTK, currentSceneNode + 0xC8, &currentSecondaryDisplayMax, 4);
    uc_mem_read(MTK, statusMeterNode + 0xC4, &meterPrimaryDisplayMax, 4);
    uc_mem_read(MTK, statusMeterNode + 0xC8, &meterSecondaryDisplayMax, 4);

    if (meterPrimaryDisplayMax != 0 || meterSecondaryDisplayMax != 0)
        return;
    if (currentPrimaryBaseMax == 0 && currentSecondaryBaseMax == 0)
        return;

    uc_mem_write(MTK, statusMeterNode + 0xC4, &currentPrimaryBaseMax, 4);
    uc_mem_write(MTK, statusMeterNode + 0xC8, &currentSecondaryBaseMax, 4);
    uc_mem_write(MTK, currentSceneNode + 0xC4, &currentPrimaryBaseMax, 4);
    uc_mem_write(MTK, currentSceneNode + 0xC8, &currentSecondaryBaseMax, 4);

    vm_net_trace("trace_status_meter_seed_fallback pc=%08x last=%08x tick=%u n2=%u sourceHead=%08x currentNode=%08x meterNode=%08x seedDisplay=%u/%u activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lastAddress,
                 g_schedulerTick,
                 n2,
                 sourceHead,
                 currentSceneNode,
                 statusMeterNode,
                 currentPrimaryBaseMax,
                 currentSecondaryBaseMax,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_loading_overlay_call(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 r0 = 0;
    u32 stateByte = 0;
    u32 sceneObj = 0;
    u8 sceneTickGate3 = 0;
    u8 sceneTickGate4 = 0;
    char uiName[96];
    uiName[0] = 0;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 19638, &stateByte, 1);
        uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
        uc_mem_read(MTK, Global_R9 + 23655, &sceneTickGate3, 1);
        uc_mem_read(MTK, Global_R9 + 23656, &sceneTickGate4, 1);
    }
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("trace_loading_overlay_call label=%s pc=%08x lr=%08x last=%08x tick=%u r0=%08x overlayState=%u sceneTickGate=%u,%u sceneObj=%08x uiName=%s activeScreen=%08x currentThis=%08x\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 stateByte,
                 sceneTickGate3,
                 sceneTickGate4,
                 sceneObj,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static bool vm_is_scene_bootstrap_loading_overlay_caller(u32 lr)
{
    switch (lr & ~1u)
    {
    case 0x100F826:
    case 0x100F89C:
    case 0x101359C:
    case 0x101366A:
    case 0x10136BC:
    case 0x1013776:
    case 0x10137B0:
    case 0x10137BE:
    case 0x10137E4:
    case 0x10137F6:
        return true;
    default:
        return false;
    }
}

static void vm_trace_scene_bootstrap_loading_overlay_candidate(u32 pc, u32 lr)
{
    static u32 s_overlayCandidateTraceCount = 0;
    u8 sceneTickGate3 = 0;
    u8 sceneTickGate4 = 0;
    u8 load0 = 0;
    u8 load1 = 0;
    u8 load2 = 0;

    if (s_overlayCandidateTraceCount >= 24)
        return;
    ++s_overlayCandidateTraceCount;
    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 23655, &sceneTickGate3, 1);
        uc_mem_read(MTK, Global_R9 + 23656, &sceneTickGate4, 1);
        uc_mem_read(MTK, Global_R9 + 23673, &load0, 1);
        uc_mem_read(MTK, Global_R9 + 23674, &load1, 1);
        uc_mem_read(MTK, Global_R9 + 23675, &load2, 1);
    }
    vm_net_trace("trace_loading_overlay_candidate pc=%08x lr=%08x caller=%08x last=%08x tick=%u sceneTickGate=%u,%u loadFlags=%u,%u,%u activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 sceneTickGate3,
                 sceneTickGate4,
                 load0,
                 load1,
                 load2,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_scene_runtime_tick(const char *label, u32 pc)
{
    static u32 s_sceneTickTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r4 = 0;
    u32 r6 = 0;
    u32 sceneObj = 0;
    u32 sceneSubObj = 0;
    u8 mode1 = 0;
    u8 pendingResync = 0;
    u8 tickFlag0 = 0;
    u8 tickFlag1 = 0;
    u8 tickFlag2 = 0;
    u8 tickGate3 = 0;
    u8 tickGate4 = 0;
    u8 auxGateD = 0;
    u8 sceneTickGate3 = 0;
    u8 sceneTickGate4 = 0;
    u8 load0 = 0;
    u8 load1 = 0;
    u8 load2 = 0;
    char uiName[96];

    if (s_sceneTickTraceCount >= 320 || !Global_R9)
        return;

    uiName[0] = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
    uc_mem_read(MTK, Global_R9 + 23655, &sceneTickGate3, 1);
    uc_mem_read(MTK, Global_R9 + 23656, &sceneTickGate4, 1);
    uc_mem_read(MTK, Global_R9 + 23673, &load0, 1);
    uc_mem_read(MTK, Global_R9 + 23674, &load1, 1);
    uc_mem_read(MTK, Global_R9 + 23675, &load2, 1);
    if (sceneObj)
    {
        uc_mem_read(MTK, sceneObj + 1, &mode1, 1);
        uc_mem_read(MTK, sceneObj + 2, &pendingResync, 1);
        uc_mem_read(MTK, sceneObj + 0x40, &sceneSubObj, 4);
    }
    if (r4)
    {
        uc_mem_read(MTK, r4 + 0, &tickFlag0, 1);
        uc_mem_read(MTK, r4 + 1, &tickFlag1, 1);
        uc_mem_read(MTK, r4 + 2, &tickFlag2, 1);
        uc_mem_read(MTK, r4 + 3, &tickGate3, 1);
        uc_mem_read(MTK, r4 + 4, &tickGate4, 1);
    }
    if (r6)
        uc_mem_read(MTK, r6 + 0x0D, &auxGateD, 1);

    if (label && strcmp(label, "oneshot_sync_check") == 0 && tickFlag1 == 0 && load0 == 0)
        return;

    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    ++s_sceneTickTraceCount;
    vm_net_trace("trace_scene_runtime_tick label=%s pc=%08x lr=%08x last=%08x tick=%u seq=%u r0=%08x r4=%08x r6=%08x"
                 " tickFlags=%u,%u,%u,%u,%u auxGateD=%u"
                 " sceneObj=%08x subObj=%08x mode1=%u pendingResync=%u sceneTickGate=%u,%u loadFlags=%u,%u,%u"
                 " uiName=%s activeScreen=%08x currentThis=%08x\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 s_sceneTickTraceCount,
                 r0,
                 r4,
                 r6,
                 tickFlag0,
                 tickFlag1,
                 tickFlag2,
                 tickGate3,
                 tickGate4,
                 auxGateD,
                 sceneObj,
                 sceneSubObj,
                 mode1,
                 pendingResync,
                 sceneTickGate3,
                 sceneTickGate4,
                 load0,
                 load1,
                 load2,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_current_actor_motion_state(const char *label, u32 pc)
{
    static u32 s_actorMotionStateTraceCount = 0;
    static u32 s_lastActorMotionStateTick = 0;
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 actorId = 0;
    u32 visualResId = 0;
    u32 drawAt = 0;
    u32 step = 0;
    u32 lr = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 targetX = 0;
    u16 targetY = 0;
    u8 nodeKind = 0;
    u8 occupied = 0;
    u8 visualGroup = 0;
    u8 visualVariant = 0;
    char labelText[64];

    if (!Global_R9 || s_actorMotionStateTraceCount >= 160)
        return;
    if (s_lastActorMotionStateTick != 0 && g_schedulerTick - s_lastActorMotionStateTick < 5)
        return;

    memset(labelText, 0, sizeof(labelText));
    hudState = Global_R9 + 0x5C64;
    if (uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4) != UC_ERR_OK || currentSceneNode == 0)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, currentSceneNode + 0x64, &actorId, 4);
    uc_mem_read(MTK, currentSceneNode + 0x24, &visualResId, 4);
    uc_mem_read(MTK, currentSceneNode + 0x18, &gridX, 2);
    uc_mem_read(MTK, currentSceneNode + 0x1A, &gridY, 2);
    uc_mem_read(MTK, currentSceneNode + 0x11E, &targetX, 2);
    uc_mem_read(MTK, currentSceneNode + 0x120, &targetY, 2);
    uc_mem_read(MTK, currentSceneNode + 0x13B, &nodeKind, 1);
    uc_mem_read(MTK, currentSceneNode + 0x13F, &occupied, 1);
    uc_mem_read(MTK, currentSceneNode + 0x140, &visualGroup, 1);
    uc_mem_read(MTK, currentSceneNode + 0x141, &visualVariant, 1);
    uc_mem_read(MTK, currentSceneNode + 0x148, &drawAt, 4);
    uc_mem_read(MTK, currentSceneNode + 0x14C, &step, 4);
    vm_net_read_guest_best_effort_label(currentSceneNode + 0x44, labelText, sizeof(labelText));

    s_lastActorMotionStateTick = g_schedulerTick;
    ++s_actorMotionStateTraceCount;
    vm_net_trace("trace_current_actor_motion_state label=%s pc=%08x lr=%08x last=%08x tick=%u node=%08x"
                 " actorId=%u name=%s grid=%u,%u target=%u,%u visual=%u,%u visualRes=%08x"
                 " nodeKind=%u occupied=%u drawAt=%08x step=%08x activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 actorId,
                 labelText,
                 (unsigned int)gridX,
                 (unsigned int)gridY,
                 (unsigned int)targetX,
                 (unsigned int)targetY,
                 (unsigned int)visualGroup,
                 (unsigned int)visualVariant,
                 visualResId,
                 (unsigned int)nodeKind,
                 (unsigned int)occupied,
                 drawAt,
                 step,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMotionStateTraceCount);
}

static void vm_net_trace_draw_map_tile_entry(u32 pc)
{
    static u32 s_drawMapTileAnomalyCount = 0;
    u32 lr = 0;
    u32 tileId = 0;
    u32 x = 0;
    u32 y = 0;
    u32 w = 0;
    u32 h = 0;
    u32 repeat = 0;
    u32 mapMgr = 0;
    u32 tileInfoBase = 0;
    u32 resourceTable = 0;
    u32 resourcePtr = 0;
    int tileIndex = -1;
    u16 imgW = 0;
    u16 imgH = 0;

    if (!Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &tileId);
    uc_reg_read(MTK, UC_ARM_REG_R1, &x);
    uc_reg_read(MTK, UC_ARM_REG_R2, &y);
    uc_reg_read(MTK, UC_ARM_REG_R3, &w);

    {
        u32 sp = 0;
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        if (sp)
        {
            uc_mem_read(MTK, sp, &h, 4);
            uc_mem_read(MTK, sp + 4, &repeat, 4);
        }
    }

    uc_mem_read(MTK, Global_R9 + 39664, &mapMgr, 4);
    if (mapMgr)
    {
        uc_mem_read(MTK, mapMgr + 4, &tileInfoBase, 4);
        uc_mem_read(MTK, mapMgr, &resourceTable, 4);
        if (resourceTable)
            uc_mem_read(MTK, resourceTable + 0x10, &resourceTable, 4);
    }
    if (tileInfoBase && tileId < 1024)
    {
        short signedIndex = -1;
        uc_mem_read(MTK, tileInfoBase + tileId * 8 + 2, &signedIndex, 2);
        tileIndex = (int)signedIndex;
        if (tileIndex >= 0 && resourceTable)
        {
            uc_mem_read(MTK, resourceTable + (u32)tileIndex * 4, &resourcePtr, 4);
            if (resourcePtr)
            {
                uc_mem_read(MTK, resourcePtr + 4, &imgW, 2);
                uc_mem_read(MTK, resourcePtr + 6, &imgH, 2);
            }
        }
    }

    if (resourcePtr != 0 && tileIndex >= 0)
        return;
    if (s_drawMapTileAnomalyCount >= 32)
        return;

    ++s_drawMapTileAnomalyCount;
    vm_net_trace("trace_draw_map_tile_entry pc=%08x lr=%08x last=%08x tick=%u tileId=%u x=%u y=%u w=%u h=%u repeat=%u mapMgr=%08x tileInfo=%08x tileIndex=%d resTable=%08x resPtr=%08x img=%ux%u activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 tileId,
                 x,
                 y,
                 w,
                 h,
                 repeat,
                 mapMgr,
                 tileInfoBase,
                 tileIndex,
                 resourceTable,
                 resourcePtr,
                 (u32)imgW,
                 (u32)imgH,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_piclib_slot_load(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 piclib = 0;
    u32 tileId = 0;
    u32 namesBase = 0;
    u32 tileInfoBase = 0;
    u32 namePtr = 0;
    int tileIndex = -1;
    u8 loadedFlag = 0;
    char nameBuf[96];

    nameBuf[0] = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (pc == 0x10433BA || pc == 0x10433DE)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &piclib);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tileId);
    }
    else
    {
        u32 r4 = 0;
        u32 r5 = 0;
        uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
        uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
        piclib = r4;
        tileId = r5 >> 3;
    }

    if (tileId != 25 && tileId != 26 && tileId != 29 &&
        tileId != 30 && tileId != 31 && tileId != 65)
        return;

    if (piclib)
    {
        uc_mem_read(MTK, piclib + 4, &tileInfoBase, 4);
        uc_mem_read(MTK, piclib + 8, &namesBase, 4);
        if (tileInfoBase)
        {
            short signedIndex = -1;
            uc_mem_read(MTK, tileInfoBase + tileId * 8, &loadedFlag, 1);
            uc_mem_read(MTK, tileInfoBase + tileId * 8 + 2, &signedIndex, 2);
            tileIndex = (int)signedIndex;
        }
        if (namesBase)
            uc_mem_read(MTK, namesBase + tileId * 4, &namePtr, 4);
    }
    if (namePtr)
        vm_net_read_guest_ascii_label(namePtr, nameBuf, sizeof(nameBuf));

    vm_net_trace("trace_piclib_slot_load label=%s pc=%08x lr=%08x last=%08x tick=%u piclib=%08x tileId=%u loaded=%u tileIndex=%d namesBase=%08x namePtr=%08x name=%s activeScreen=%08x currentThis=%08x\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 piclib,
                 tileId,
                 (u32)loadedFlag,
                 tileIndex,
                 namesBase,
                 namePtr,
                 nameBuf,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_scene_colnames_entry(u32 pc, u32 lr, u32 packet)
{
    u32 colnum = 0;
    u32 colblob = 0;
    if (packet)
    {
        uc_mem_read(MTK, packet + 76, &colnum, 4);
        uc_mem_read(MTK, packet + 40, &colblob, 4);
    }
    vm_net_trace("trace_scene_colnames_entry pc=%08x lr=%08x last=%08x tick=%u packet=%08x rawFnColnum=%08x rawFnColnames=%08x\n",
                 pc, lr, lastAddress, g_schedulerTick, packet, colnum, colblob);
}

static void vm_net_trace_scene_colnames_item(u32 pc, u32 lr, u32 index, u32 len, u32 ptr)
{
    char nameBuf[96];
    vm_net_read_guest_ascii_label(ptr, nameBuf, sizeof(nameBuf));
    vm_net_trace("trace_scene_colnames_item pc=%08x lr=%08x last=%08x tick=%u index=%u len=%u ptr=%08x text=%s\n",
                 pc, lr, lastAddress, g_schedulerTick, index, len, ptr, nameBuf);
}

static void vm_net_trace_scene_list_return(const char *label, u32 pc, u32 lr)
{
    u32 packet = 0;
    u32 kind = 0;
    u32 subtype = 0;
    u8 sceneGate = 0;
    u8 sceneState = 0;
    u8 load0 = 0;
    u8 load1 = 0;
    u8 load2 = 0;
    uc_reg_read(MTK, UC_ARM_REG_R0, &packet);
    if (packet)
    {
        uc_mem_read(MTK, packet + 4, &kind, 4);
        uc_mem_read(MTK, packet + 8, &subtype, 4);
    }
    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 21900, &sceneGate, 1);
        uc_mem_read(MTK, Global_R9 + 21896, &sceneState, 1);
        uc_mem_read(MTK, Global_R9 + 23673, &load0, 1);
        uc_mem_read(MTK, Global_R9 + 23674, &load1, 1);
        uc_mem_read(MTK, Global_R9 + 23675, &load2, 1);
    }
    vm_net_trace("trace_scene_list_return label=%s pc=%08x lr=%08x last=%08x tick=%u packet=%08x kind=%u subtype=%u sceneGate=%u sceneState=%u loadFlags=%u,%u,%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 packet,
                 kind,
                 subtype,
                 sceneGate,
                 sceneState,
                 load0,
                 load1,
                 load2);
}

static void vm_net_trace_scene_enter_apply(const char *label, u32 pc)
{
    static u32 s_sceneEnterApplyTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r5 = 0;
    u32 r6 = 0;
    u32 sp = 0;
    u32 stack0 = 0;
    u32 stack4 = 0;
    u32 sceneObj = 0;
    u8 sceneGate = 0;
    u16 sceneState = 0;
    char sceneName[96];

    if (s_sceneEnterApplyTraceCount >= 64)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    if (sp)
    {
        (void)uc_mem_read(MTK, sp, &stack0, 4);
        (void)uc_mem_read(MTK, sp + 4, &stack4, 4);
    }

    if (Global_R9)
        (void)uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
    if (sceneObj)
    {
        (void)uc_mem_read(MTK, sceneObj + 356, &sceneGate, 1);
        (void)uc_mem_read(MTK, sceneObj + 436, &sceneState, 2);
    }

    vm_net_read_guest_best_effort_label(r0, sceneName, sizeof(sceneName));
    ++s_sceneEnterApplyTraceCount;
    vm_net_trace("trace_scene_enter_apply label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " scenePtr=%08x scene=%s len=%u x=%u y=%u stack0=%08x stack4=%08x sceneObj=%08x slotR5=%08x cbR6=%08x cbR1=%08x"
                 " sceneGate=%u sceneState=%u activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 sceneName,
                 r1,
                 r2,
                 r3,
                 stack0,
                 stack4,
                 sceneObj,
                 r5,
                 r6,
                 r1,
                 sceneGate,
                 sceneState,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneEnterApplyTraceCount);
}

static void vm_net_trace_same_class_scene_table(const char *label, u32 pc)
{
    static u32 s_sameClassSceneTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 r6 = 0;
    u32 hudState = 0;
    u32 sceneObj = 0;
    u32 currentNode = 0;
    u32 tableBase = 0;
    u32 tableCount = 0;
    u32 pendingMode = 0;
    u8 sceneClass = 0;
    u8 pendingState = 0;
    u8 sameClassMode = 0;
    u16 pendingX = 0;
    u16 pendingY = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    char pendingScene[96];

    if (!Global_R9 || s_sameClassSceneTraceCount >= 320)
        return;

    pendingScene[0] = 0;
    hudState = Global_R9 + 0x5C64;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);

    (void)uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
    (void)uc_mem_read(MTK, hudState + 0x40, &currentNode, 4);
    (void)uc_mem_read(MTK, hudState + 0x80, &tableBase, 4);
    (void)uc_mem_read(MTK, hudState + 0xDC, &tableCount, 4);
    (void)uc_mem_read(MTK, hudState + 0x11, &sceneClass, 1);
    (void)uc_mem_read(MTK, hudState + 0x12, &pendingState, 1);
    (void)uc_mem_read(MTK, hudState + 0x1E, &sameClassMode, 1);
    (void)uc_mem_read(MTK, hudState + 0x28, &pendingMode, 4);
    (void)uc_mem_read(MTK, hudState + 0x2A, &pendingX, 2);
    (void)uc_mem_read(MTK, hudState + 0x2C, &pendingY, 2);
    if (currentNode)
    {
        (void)uc_mem_read(MTK, currentNode + 0x18, &gridX, 2);
        (void)uc_mem_read(MTK, currentNode + 0x1A, &gridY, 2);
    }
    if (sceneObj)
        vm_net_read_guest_best_effort_label(sceneObj + 0x475, pendingScene, sizeof(pendingScene));

    ++s_sameClassSceneTraceCount;
    vm_net_trace("trace_same_class_scene_table label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " r0=%08x r4=%08x r5=%08x r6=%08x sceneObj=%08x currentNode=%08x grid=%u,%u"
                 " pendingScene=%s pendingMode=%08x pendingPos=%u,%u class=%u pendingState=%u sameClassMode=%u"
                 " tableBase=%08x tableCount=%u activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r4,
                 r5,
                 r6,
                 sceneObj,
                 currentNode,
                 (u32)gridX,
                 (u32)gridY,
                 pendingScene,
                 pendingMode,
                 (u32)pendingX,
                 (u32)pendingY,
                 (u32)sceneClass,
                 (u32)pendingState,
                 (u32)sameClassMode,
                 tableBase,
                 tableCount,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sameClassSceneTraceCount);

    if (tableBase && tableCount)
    {
        u32 limit = tableCount < 6 ? tableCount : 6;
        for (u32 i = 0; i < limit; ++i)
        {
            u32 entry = tableBase + i * 0x20;
            u32 scenePtr = 0;
            u32 countdown = 0;
            u16 entryMode = 0;
            u16 x1 = 0;
            u16 y1 = 0;
            u16 x2 = 0;
            u16 y2 = 0;
            u8 state = 0;
            char sceneName[96];
            sceneName[0] = 0;
            (void)uc_mem_read(MTK, entry + 0x00, &entryMode, 2);
            (void)uc_mem_read(MTK, entry + 0x06, &x1, 2);
            (void)uc_mem_read(MTK, entry + 0x08, &y1, 2);
            (void)uc_mem_read(MTK, entry + 0x0A, &x2, 2);
            (void)uc_mem_read(MTK, entry + 0x0C, &y2, 2);
            (void)uc_mem_read(MTK, entry + 0x10, &scenePtr, 4);
            (void)uc_mem_read(MTK, entry + 0x17, &state, 1);
            (void)uc_mem_read(MTK, entry + 0x1C, &countdown, 4);
            if (scenePtr)
                vm_net_read_guest_best_effort_label(scenePtr, sceneName, sizeof(sceneName));
            vm_net_trace("trace_same_class_scene_table_entry label=%s index=%u entry=%08x mode=%u rect=%u,%u-%u,%u"
                         " scenePtr=%08x scene=%s state=%u countdown=%u tick=%u\n",
                         label ? label : "?",
                         i,
                         entry,
                         (u32)entryMode,
                         (u32)x1,
                         (u32)y1,
                         (u32)x2,
                         (u32)y2,
                         scenePtr,
                         sceneName,
                         (u32)state,
                         countdown,
                         g_schedulerTick);
        }
    }
}

static void vm_net_trace_same_class_node_callback(const char *label, u32 pc)
{
    static u32 s_sameClassNodeCallbackTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 hudState = 0;
    u32 sceneObj = 0;
    u32 currentNode = 0;
    u32 nodeArg = 0;
    u32 resetTarget = 0;
    u32 drawAt = 0;
    u32 step = 0;
    u32 nodeArgDrawAt = 0;
    u32 nodeArgStep = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 pendingX = 0;
    u16 pendingY = 0;
    u16 nodeArgGridX = 0;
    u16 nodeArgGridY = 0;
    u16 pendingMode = 0;
    char pendingScene[96];
    char currentName[64];
    char nodeArgName[64];

    if (!Global_R9 || s_sameClassNodeCallbackTraceCount >= 96)
        return;

    pendingScene[0] = 0;
    currentName[0] = 0;
    nodeArgName[0] = 0;
    hudState = Global_R9 + 0x5C64;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);

    (void)uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
    (void)uc_mem_read(MTK, hudState + 0x40, &currentNode, 4);
    (void)uc_mem_read(MTK, hudState + 0x44, &resetTarget, 4);
    (void)uc_mem_read(MTK, hudState + 0x28, &pendingMode, 2);
    (void)uc_mem_read(MTK, hudState + 0x2A, &pendingX, 2);
    (void)uc_mem_read(MTK, hudState + 0x2C, &pendingY, 2);
    nodeArg = r1;

    if (sceneObj)
        vm_net_read_guest_best_effort_label(sceneObj + 0x475, pendingScene, sizeof(pendingScene));
    if (currentNode)
    {
        (void)uc_mem_read(MTK, currentNode + 0x18, &gridX, 2);
        (void)uc_mem_read(MTK, currentNode + 0x1A, &gridY, 2);
        (void)uc_mem_read(MTK, currentNode + 0x148, &drawAt, 4);
        (void)uc_mem_read(MTK, currentNode + 0x14C, &step, 4);
        vm_net_read_guest_best_effort_label(currentNode + 0x44, currentName, sizeof(currentName));
    }
    if (nodeArg)
    {
        (void)uc_mem_read(MTK, nodeArg + 0x18, &nodeArgGridX, 2);
        (void)uc_mem_read(MTK, nodeArg + 0x1A, &nodeArgGridY, 2);
        (void)uc_mem_read(MTK, nodeArg + 0x148, &nodeArgDrawAt, 4);
        (void)uc_mem_read(MTK, nodeArg + 0x14C, &nodeArgStep, 4);
        vm_net_read_guest_best_effort_label(nodeArg + 0x44, nodeArgName, sizeof(nodeArgName));
    }

    ++s_sameClassNodeCallbackTraceCount;
    vm_net_trace("trace_same_class_node_callback label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " r0=%08x r1=%08x r2=%08x r3=%08x sceneObj=%08x pendingScene=%s pendingMode=%u pendingPos=%u,%u"
                 " currentNode=%08x currentName=%s currentGrid=%u,%u currentDraw=%08x currentStep=%08x"
                 " resetTarget=%08x nodeArg=%08x nodeArgName=%s nodeArgGrid=%u,%u nodeArgDraw=%08x nodeArgStep=%08x count=%u\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 sceneObj,
                 pendingScene,
                 (u32)pendingMode,
                 (u32)pendingX,
                 (u32)pendingY,
                 currentNode,
                 currentName,
                 (u32)gridX,
                 (u32)gridY,
                 drawAt,
                 step,
                 resetTarget,
                 nodeArg,
                 nodeArgName,
                 (u32)nodeArgGridX,
                 (u32)nodeArgGridY,
                 nodeArgDrawAt,
                 nodeArgStep,
                 s_sameClassNodeCallbackTraceCount);
}

static void vm_net_trace_mem(const char *tag, u32 ptr, u32 len)
{
    if (ptr == 0 || len == 0)
        return;
    u8 data[160];
    u32 dumpLen = len < sizeof(data) ? len : (u32)sizeof(data);
    if (uc_mem_read(MTK, ptr, data, dumpLen) != UC_ERR_OK)
    {
        vm_net_trace("%s ptr=%08x len=%u read_fail\n", tag, ptr, len);
        return;
    }
    char line[96];
    snprintf(line, sizeof(line), "%s ptr=%08x", tag, ptr);
    vm_net_trace_bytes(line, data, dumpLen);
}

static void vm_net_trace_wt_field_table_detail(const char *label, u32 obj, u32 pc)
{
    static u32 s_wtFieldTableTraceCount = 0;
    u32 lr = 0;
    u32 kind = 0;
    u32 subtype = 0;
    u32 fieldCount = 0;
    u32 fieldCap = 0;
    u32 fieldTable = 0;
    u32 rawGet = 0;
    u32 stringGet = 0;
    u32 u32Get = 0;
    u32 u8Get = 0;
    u32 lenGet = 0;
    u32 i = 0;

    if (obj == 0 || s_wtFieldTableTraceCount >= 192)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    (void)vm_net_trace_read_u32(obj + 0x04u, &kind);
    (void)vm_net_trace_read_u32(obj + 0x08u, &subtype);
    (void)vm_net_trace_read_u32(obj + 0x10u, &fieldCount);
    (void)vm_net_trace_read_u32(obj + 0x14u, &fieldCap);
    (void)vm_net_trace_read_u32(obj + 0x18u, &fieldTable);
    (void)vm_net_trace_read_u32(obj + 0x28u, &rawGet);
    (void)vm_net_trace_read_u32(obj + 0x40u, &stringGet);
    (void)vm_net_trace_read_u32(obj + 0x44u, &u32Get);
    (void)vm_net_trace_read_u32(obj + 0x4Cu, &u8Get);
    (void)vm_net_trace_read_u32(obj + 0x54u, &lenGet);

    ++s_wtFieldTableTraceCount;
    vm_net_trace("trace_wt_field_table label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " obj=%08x kind=%u subtype=%u fieldCount=%u fieldCap=%u fieldTable=%08x"
                 " methods=%08x,%08x,%08x,%08x,%08x count=%u"
                 " evidence=runtime:WT_object_field_table_entry_12_bytes_name_value_meta\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 obj,
                 kind,
                 subtype,
                 fieldCount,
                 fieldCap,
                 fieldTable,
                 rawGet,
                 stringGet,
                 u32Get,
                 u8Get,
                 lenGet,
                 s_wtFieldTableTraceCount);

    if (fieldTable == 0)
        return;
    if (fieldCount > 8)
        fieldCount = 8;
    for (i = 0; i < fieldCount; ++i)
    {
        u32 entry = fieldTable + i * 12u;
        u32 namePtr = 0;
        u32 valuePtr = 0;
        u32 meta = 0;
        u16 taggedLen = 0;
        u8 bytes[16] = {0};
        u8 lenBytes[2] = {0};
        char name[64];
        char valueHex[96];
        u32 dumpLen = 0;

        name[0] = '\0';
        valueHex[0] = '\0';
        (void)vm_net_trace_read_u32(entry + 0x00u, &namePtr);
        (void)vm_net_trace_read_u32(entry + 0x04u, &valuePtr);
        (void)vm_net_trace_read_u32(entry + 0x08u, &meta);
        vm_net_read_guest_best_effort_label(namePtr, name, sizeof(name));
        if (valuePtr != 0)
        {
            if (uc_mem_read(MTK, valuePtr, lenBytes, sizeof(lenBytes)) == UC_ERR_OK)
                taggedLen = (u16)(((u16)lenBytes[0] << 8) | (u16)lenBytes[1]);
            dumpLen = taggedLen + 2u;
            if (dumpLen > sizeof(bytes))
                dumpLen = sizeof(bytes);
            if (dumpLen == 0)
                dumpLen = sizeof(bytes);
            if (uc_mem_read(MTK, valuePtr, bytes, dumpLen) == UC_ERR_OK)
                vm_format_trace_bytes_hex(bytes, dumpLen, valueHex, sizeof(valueHex));
        }
        vm_net_trace("trace_wt_field_entry label=%s obj=%08x idx=%u entry=%08x namePtr=%08x name=%s valuePtr=%08x meta=%08x taggedLen=%u valueHead=%s evidence=runtime:WT_field_entry_name_value_meta\n",
                     label ? label : "unknown",
                     obj,
                     i,
                     entry,
                     namePtr,
                     name,
                     valuePtr,
                     meta,
                     (unsigned int)taggedLen,
                     valueHex);
    }
}

static void vm_net_trace_battle_server_cmd_detail(const char *label, u32 pc)
{
    static u32 s_battleServerCmdTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 packet = 0;
    u32 packetKind = 0;
    u32 packetSubtype = 0;
    u32 battleState = 0;
    u32 battleMain = 0;
    u32 phaseWord = 0;
    u32 state5c = 0;
    u8 state06 = 0;
    u8 state0a = 0;
    u8 state0b = 0;
    u8 state0c = 0;
    u8 state0d = 0;
    u8 state0e = 0;
    u16 ownCount = 0;
    u16 enemyCount = 0;

    if (s_battleServerCmdTraceCount >= 192)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    packet = r7 != 0 ? r7 : r1;
    (void)vm_net_trace_read_u32(packet + 0x04u, &packetKind);
    (void)vm_net_trace_read_u32(packet + 0x08u, &packetSubtype);
    battleState = r9 + 0x3450u;
    (void)vm_net_trace_read_u32(r9 + 0x2050u, &battleMain);
    (void)vm_net_trace_read_u32(battleState + 0x5Cu, &state5c);
    (void)vm_net_trace_read_u8(battleState + 0x06u, &state06);
    (void)vm_net_trace_read_u8(battleState + 0x0Au, &state0a);
    (void)vm_net_trace_read_u8(battleState + 0x0Bu, &state0b);
    (void)vm_net_trace_read_u8(battleState + 0x0Cu, &state0c);
    (void)vm_net_trace_read_u8(battleState + 0x0Du, &state0d);
    (void)vm_net_trace_read_u8(battleState + 0x0Eu, &state0e);
    (void)vm_net_trace_read_u16(battleState + 0x1Au, &ownCount);
    (void)vm_net_trace_read_u16(battleState + 0x1Cu, &enemyCount);
    if (battleMain != 0)
        (void)vm_net_trace_read_u32(battleMain + 0x470u, &phaseWord);

    ++s_battleServerCmdTraceCount;
    vm_net_trace("trace_battle_server_cmd label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " packet=%08x kind=%u subtype=%u regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x"
                 " battleState=%08x battleMain=%08x state06=%u state0a0b0c0d0e=%u,%u,%u,%u,%u state5c=%u counts=%u,%u phaseWord=%08x"
                 " mockHp=%u/%u,%u/%u count=%u"
                 " evidence=Battle.cbm:HandleServerBattleCmd_0x7BD0_kind4_case_map\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 packet,
                 packetKind,
                 packetSubtype,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 battleState,
                 battleMain,
                 (unsigned int)state06,
                 (unsigned int)state0a,
                 (unsigned int)state0b,
                 (unsigned int)state0c,
                 (unsigned int)state0d,
                 (unsigned int)state0e,
                 state5c,
                 (unsigned int)ownCount,
                 (unsigned int)enemyCount,
                 phaseWord,
                 g_mockBattleRoleHpCurrent,
                 g_mockBattleRoleHpMax,
                 g_mockBattleEnemyHpCurrent,
                 g_mockBattleEnemyHpMax,
                 s_battleServerCmdTraceCount);

    vm_net_trace_wt_field_table_detail(label ? label : "battle_server_cmd", packet, pc);
    if (battleState != 0)
        vm_net_trace_mem("trace_battle_server_cmd_state", battleState, 0x80);
}

static void vm_net_trace_battle_operate_subtype8_detail(const char *label, u32 pc)
{
    static u32 s_battleOperateSubtype8TraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 stackInfoPtr = 0;
    u32 parserObj = 0;
    u32 parserBuf = 0;
    u32 parserState = 0;
    u32 parserBufCap = 0;
    u32 battleState = 0;
    u32 infoDstBuf = 0;
    u32 infoDstDumpLen = 32;
    u8 parserFlagA = 0;
    u8 parserFlagB = 0;
    u8 parserFlagC = 0;
    u8 parserFlagD = 0;

    if (s_battleOperateSubtype8TraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    (void)vm_net_trace_read_u32(sp + 8u, &stackInfoPtr);
    if (vm_net_trace_read_u32(r5, &parserObj) && parserObj != 0)
    {
        (void)vm_net_trace_read_u32(parserObj + 0x30u, &parserBuf);
        (void)vm_net_trace_read_u32(parserObj + 0x34u, &parserBufCap);
        (void)vm_net_trace_read_u32(parserObj + 0x5Cu, &parserState);
    }
    battleState = r9 + 0x3450u;
    (void)vm_net_trace_read_u32(battleState + 0x30u, &infoDstBuf);
    if (stackInfoPtr != 0)
        infoDstDumpLen = 16;
    (void)vm_net_trace_read_u8(battleState + 0x0Au, &parserFlagA);
    (void)vm_net_trace_read_u8(battleState + 0x0Bu, &parserFlagB);
    (void)vm_net_trace_read_u8(battleState + 0x0Cu, &parserFlagC);
    (void)vm_net_trace_read_u8(battleState + 0x0Du, &parserFlagD);

    ++s_battleOperateSubtype8TraceCount;
    vm_net_trace("trace_battle_operate_subtype8_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " stackInfoPtr=%08x parserObj=%08x parserBuf=%08x parserBufCap=%08x parserState=%08x battleState=%08x infoDstBuf=%08x parserFlags=%u,%u,%u,%u count=%u"
                 " evidence=Battle.cbm:sub_7BD0_subtype8_info_copy_and_apply_window\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 stackInfoPtr,
                 parserObj,
                 parserBuf,
                 parserBufCap,
                 parserState,
                 battleState,
                 infoDstBuf,
                 (unsigned int)parserFlagA,
                 (unsigned int)parserFlagB,
                 (unsigned int)parserFlagC,
                 (unsigned int)parserFlagD,
                 s_battleOperateSubtype8TraceCount);

    if (r1 != 0 && r2 != 0 && r2 < 0x400u)
        vm_net_trace_mem("trace_battle_operate_subtype8_src", r1, r2);
    else if (stackInfoPtr != 0 && r7 != 0 && r7 < 0x400u)
        vm_net_trace_mem("trace_battle_operate_subtype8_stack_src", stackInfoPtr, r7);
    if (stackInfoPtr != 0)
        vm_net_trace_mem("trace_battle_operate_subtype8_info_src_wrapper", stackInfoPtr, 32);
    if (parserObj != 0)
        vm_net_trace_mem("trace_battle_operate_subtype8_obj", parserObj, 96);
    if (parserBuf != 0)
        vm_net_trace_mem("trace_battle_operate_subtype8_dst", parserBuf, 96);
    if (infoDstBuf != 0)
    {
        vm_net_trace_mem("trace_battle_operate_subtype8_info_dst", infoDstBuf, infoDstDumpLen);
        vm_net_trace_battle_arm_subtype8_info_dst_watch(infoDstBuf, (r7 < 0x400u) ? (r7 + 1u) : infoDstDumpLen, pc);
    }
}

static void vm_net_trace_battle_arm_subtype8_info_dst_watch(u32 base, u32 len, u32 pc)
{
    if (base == 0)
        return;

    g_battleSubtype8InfoDstWatchBase = base;
    g_battleSubtype8InfoDstWatchLen = len != 0 ? len : 0x20u;
    if (g_battleSubtype8InfoDstWatchLen > 0x100u)
        g_battleSubtype8InfoDstWatchLen = 0x100u;
    g_battleSubtype8InfoDstWatchTick = g_schedulerTick;
    g_battleSubtype8InfoDstWriteTraceCount = 0;

    vm_net_trace("trace_battle_subtype8_info_dst_watch_arm base=%08x len=%u pc=%08x tick=%u evidence=runtime:subtype8_info_dst_staging_watch\n",
                 g_battleSubtype8InfoDstWatchBase,
                 g_battleSubtype8InfoDstWatchLen,
                 pc,
                 g_schedulerTick);
}

void vm_net_trace_battle_subtype8_info_dst_write(uint64_t address, uint32_t size, int64_t value)
{
    u32 writeAddr = (u32)address;
    u32 writeEnd = 0;
    u32 watchEnd = 0;
    u32 pc = lastAddress;
    u32 lr = 0;
    u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0;

    if (g_battleSubtype8InfoDstWatchBase == 0 || g_battleSubtype8InfoDstWatchLen == 0 || size == 0)
        return;
    if (g_schedulerTick != g_battleSubtype8InfoDstWatchTick)
        return;
    if (g_battleSubtype8InfoDstWriteTraceCount >= 24)
        return;

    writeEnd = writeAddr + size;
    watchEnd = g_battleSubtype8InfoDstWatchBase + g_battleSubtype8InfoDstWatchLen;
    if (writeEnd <= g_battleSubtype8InfoDstWatchBase || writeAddr >= watchEnd)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);

    ++g_battleSubtype8InfoDstWriteTraceCount;
    vm_net_trace("trace_battle_subtype8_info_dst_write base=%08x len=%u writeAddr=%08x writeSize=%u raw=%08x pc=%08x lr=%08x last=%08x tick=%u regs=%08x,%08x,%08x,%08x count=%u evidence=runtime:subtype8_info_dst_staging_write_overlap\n",
                 g_battleSubtype8InfoDstWatchBase,
                 g_battleSubtype8InfoDstWatchLen,
                 writeAddr,
                 size,
                 (u32)value,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 g_battleSubtype8InfoDstWriteTraceCount);
    vm_net_trace_mem("trace_battle_subtype8_info_dst_write_dump", g_battleSubtype8InfoDstWatchBase, g_battleSubtype8InfoDstWatchLen);
}

static void vm_net_trace_battle_actioninfo_parser_detail(const char *label, u32 pc)
{
    static u32 s_battleActioninfoParserTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 slotTable = 0;
    u32 actionCount = 0;
    u32 loopIndex = 0;
    u32 recordBase = 0;
    u32 recordValueA = 0;
    u32 recordValueB = 0;
    u32 recordBlobId = 0;
    u8 recordUsed = 0;
    u8 recordType = 0;
    u8 recordArg = 0;
    u8 recordSubCount = 0;
    u8 recordFlag14 = 0;
    u8 recordByte15 = 0;
    u8 recordByte16 = 0;
    u8 recordTail0 = 0;
    u8 recordTail1 = 0;
    u8 recordTail2 = 0;

    if (s_battleActioninfoParserTraceCount >= 120)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    (void)vm_net_trace_read_u32(sp + 0x34u, &slotTable);
    (void)vm_net_trace_read_u32(sp + 0x30u, &actionCount);
    (void)vm_net_trace_read_u32(sp + 0x2Cu, &loopIndex);
    recordBase = r6;
    if (recordBase != 0)
    {
        (void)vm_net_trace_read_u8(recordBase + 0u, &recordUsed);
        (void)vm_net_trace_read_u8(recordBase + 1u, &recordType);
        (void)vm_net_trace_read_u8(recordBase + 2u, &recordArg);
        (void)vm_net_trace_read_u8(recordBase + 3u, &recordSubCount);
        (void)vm_net_trace_read_u8(recordBase + 0x14u, &recordFlag14);
        (void)vm_net_trace_read_u8(recordBase + 0x15u, &recordByte15);
        (void)vm_net_trace_read_u8(recordBase + 0x16u, &recordByte16);
        (void)vm_net_trace_read_u32(recordBase + 0x18u, &recordValueA);
        (void)vm_net_trace_read_u32(recordBase + 0x1Cu, &recordValueB);
        (void)vm_net_trace_read_u32(recordBase + 0x5Cu, &recordBlobId);
        (void)vm_net_trace_read_u8(recordBase + 0x60u, &recordTail0);
        (void)vm_net_trace_read_u8(recordBase + 0x61u, &recordTail1);
        (void)vm_net_trace_read_u8(recordBase + 0x62u, &recordTail2);
    }

    ++s_battleActioninfoParserTraceCount;
    vm_net_trace("trace_battle_actioninfo_parser_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " slotTable=%08x actionCount=%u loopIndex=%u recordBase=%08x"
                 " record=%u,%u,%u,%u head=%u,%u,%u vals=%08x,%08x blobId=%08x tail=%u,%u,%u count=%u"
                 " evidence=Battle.cbm:sub_actioninfo_parser_05188dd0\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 slotTable,
                 actionCount,
                 loopIndex,
                 recordBase,
                 (unsigned int)recordUsed,
                 (unsigned int)recordType,
                 (unsigned int)recordArg,
                 (unsigned int)recordSubCount,
                 (unsigned int)recordFlag14,
                 (unsigned int)recordByte15,
                 (unsigned int)recordByte16,
                 recordValueA,
                 recordValueB,
                 recordBlobId,
                 (unsigned int)recordTail0,
                 (unsigned int)recordTail1,
                 (unsigned int)recordTail2,
                 s_battleActioninfoParserTraceCount);

    if (r4 != 0)
        vm_net_trace_mem("trace_battle_actioninfo_parser_stream", r4, 64);
    if (recordBase != 0)
        vm_net_trace_mem("trace_battle_actioninfo_parser_record", recordBase, 64);
}

static void vm_net_trace_battle_actioninfo_stream_read_detail(const char *label, u32 pc)
{
    static u32 s_battleActioninfoStreamReadTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r4 = 0, r5 = 0, r6 = 0, r9 = 0;
    u32 streamWord0 = 0;
    u32 streamWord4 = 0;
    u32 streamWord8 = 0;
    u32 streamWordC = 0;
    u32 streamWord10 = 0;
    u32 streamWord14 = 0;
    u32 readerWord0 = 0;
    u32 readerWord4 = 0;
    u32 readerWord8 = 0;
    u32 readerWordC = 0;
    u32 readerWord10 = 0;
    u32 readerWord14 = 0;
    u32 readerCursor = 0;
    u32 readerBlobObj = 0;
    u32 readerBlobPtr = 0;
    u32 readerBlobLen = 0;
    u8 recType = 0;
    u8 recArg = 0;
    u8 recSubCount = 0;

    if (s_battleActioninfoStreamReadTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    if (r4 != 0)
    {
        (void)vm_net_trace_read_u32(r4 + 0x00u, &streamWord0);
        (void)vm_net_trace_read_u32(r4 + 0x04u, &streamWord4);
        (void)vm_net_trace_read_u32(r4 + 0x08u, &streamWord8);
        (void)vm_net_trace_read_u32(r4 + 0x0Cu, &streamWordC);
        (void)vm_net_trace_read_u32(r4 + 0x10u, &streamWord10);
        (void)vm_net_trace_read_u32(r4 + 0x14u, &streamWord14);
    }
    if (r5 != 0)
    {
        (void)vm_net_trace_read_u32(r5 + 0x00u, &readerWord0);
        (void)vm_net_trace_read_u32(r5 + 0x04u, &readerWord4);
        (void)vm_net_trace_read_u32(r5 + 0x08u, &readerWord8);
        (void)vm_net_trace_read_u32(r5 + 0x0Cu, &readerWordC);
        (void)vm_net_trace_read_u32(r5 + 0x10u, &readerWord10);
        (void)vm_net_trace_read_u32(r5 + 0x14u, &readerWord14);
        (void)vm_net_trace_read_u32(r5 + 0x400u, &readerCursor);
        (void)vm_net_trace_read_u32(r5 + 0x408u, &readerBlobObj);
        if (readerBlobObj != 0)
        {
            (void)vm_net_trace_read_u32(readerBlobObj + 0x04u, &readerBlobPtr);
            (void)vm_net_trace_read_u32(readerBlobObj + 0x08u, &readerBlobLen);
        }
    }
    if (r6 != 0)
    {
        (void)vm_net_trace_read_u8(r6 + 0x01u, &recType);
        (void)vm_net_trace_read_u8(r6 + 0x02u, &recArg);
        (void)vm_net_trace_read_u8(r6 + 0x03u, &recSubCount);
    }

    ++s_battleActioninfoStreamReadTraceCount;
    vm_net_trace("trace_battle_actioninfo_stream_read label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " r0=%08x r1=%08x r4=%08x r5=%08x r6=%08x r9=%08x sp=%08x"
                 " stream=%08x,%08x,%08x,%08x,%08x,%08x record=%u,%u,%u count=%u"
                 " reader=%08x,%08x,%08x,%08x,%08x,%08x cursor=%u blobObj=%08x blobPtr=%08x blobLen=%u"
                 " evidence=Battle.cbm:sub_6EB0_header_and_child_stream_reads_05188f36_05188f96\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r4,
                 r5,
                 r6,
                 r9,
                 sp,
                 streamWord0,
                 streamWord4,
                 streamWord8,
                 streamWordC,
                 streamWord10,
                 streamWord14,
                 (unsigned int)recType,
                 (unsigned int)recArg,
                 (unsigned int)recSubCount,
                 s_battleActioninfoStreamReadTraceCount,
                 readerWord0,
                 readerWord4,
                 readerWord8,
                 readerWordC,
                 readerWord10,
                 readerWord14,
                 (unsigned int)readerCursor,
                 readerBlobObj,
                 readerBlobPtr,
                 (unsigned int)readerBlobLen);

    if (r4 != 0)
        vm_net_trace_mem("trace_battle_actioninfo_stream_read_streamobj", r4, 96);
    if (r5 != 0)
        vm_net_trace_mem("trace_battle_actioninfo_stream_read_reader", r5, 96);
    if (readerBlobObj != 0)
        vm_net_trace_mem("trace_battle_actioninfo_stream_read_blobobj", readerBlobObj, 32);
    if (readerBlobPtr != 0)
        vm_net_trace_mem("trace_battle_actioninfo_stream_read_blob", readerBlobPtr, 96);
}

static void vm_net_trace_battle_actioninfo_field_result_detail(const char *label, u32 pc)
{
    static u32 s_battleActioninfoFieldResultTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 head0 = 0, head4 = 0, head8 = 0, headC = 0, head10 = 0, head14 = 0;

    if (s_battleActioninfoFieldResultTraceCount >= 64)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    ++s_battleActioninfoFieldResultTraceCount;
    vm_net_trace("trace_battle_actioninfo_field_result label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " r0=%08x r1=%08x r2=%08x r4=%08x r5=%08x r6=%08x r7=%08x r9=%08x sp=%08x count=%u"
                 " evidence=Battle.cbm:sub_6EB0_field_getter_return_sites_05188ee2_05188efe\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r4,
                 r5,
                 r6,
                 r7,
                 r9,
                 sp,
                 s_battleActioninfoFieldResultTraceCount);

    if (r0 != 0)
    {
        vm_net_trace_mem("trace_battle_actioninfo_field_result_r0", r0, 96);
        if (vm_net_trace_read_u32(r0 + 0x00u, &head0) &&
            vm_net_trace_read_u32(r0 + 0x04u, &head4) &&
            vm_net_trace_read_u32(r0 + 0x08u, &head8) &&
            vm_net_trace_read_u32(r0 + 0x0Cu, &headC) &&
            vm_net_trace_read_u32(r0 + 0x10u, &head10) &&
            vm_net_trace_read_u32(r0 + 0x14u, &head14))
        {
            vm_net_trace("trace_battle_actioninfo_field_result_head label=%s ptr=%08x"
                         " head=%08x,%08x,%08x,%08x,%08x,%08x"
                         " evidence=runtime_recursive_peek_into_actioninfo_wrapper_head\n",
                         label ? label : "unknown",
                         r0,
                         head0,
                         head4,
                         head8,
                         headC,
                         head10,
                         head14);
            if (head0 >= 0x05000000u)
                vm_net_trace_mem("trace_battle_actioninfo_field_result_head0_ptr", head0, 96);
            if (head4 >= 0x05000000u)
                vm_net_trace_mem("trace_battle_actioninfo_field_result_head4_ptr", head4, 96);
            if (head10 >= 0x05000000u)
                vm_net_trace_mem("trace_battle_actioninfo_field_result_head10_ptr", head10, 96);
            if (head14 >= 0x05000000u)
                vm_net_trace_mem("trace_battle_actioninfo_field_result_head14_ptr", head14, 96);
        }
    }
    if (r4 != 0)
        vm_net_trace_mem("trace_battle_actioninfo_field_result_r4", r4, 96);
    if (r6 != 0)
        vm_net_trace_wt_field_table_detail(label ? label : "action_field_packet", r6, pc);
}

static void vm_net_trace_battle_actioninfo_wrapper_detail(const char *label, u32 pc)
{
    static u32 s_battleActioninfoWrapperTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r4 = 0, r5 = 0, r9 = 0;
    u32 wrapperCount = 0;
    u32 wrapperCursor = 0;
    u32 reader20 = 0;
    u32 reader24 = 0;
    u32 reader28 = 0;

    if (s_battleActioninfoWrapperTraceCount >= 64)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    (void)vm_net_trace_read_u32(sp + 0x08u, &wrapperCount);
    (void)vm_net_trace_read_u32(sp + 0x404u, &wrapperCursor);
    if (r5 != 0)
    {
        (void)vm_net_trace_read_u32(r5 + 0x20u, &reader20);
        (void)vm_net_trace_read_u32(r5 + 0x24u, &reader24);
        (void)vm_net_trace_read_u32(r5 + 0x28u, &reader28);
    }

    ++s_battleActioninfoWrapperTraceCount;
    vm_net_trace("trace_battle_actioninfo_wrapper_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " r0=%08x r1=%08x r4=%08x r5=%08x r9=%08x sp=%08x count=%u cursor=%u"
                 " readers=%08x,%08x,%08x wrapCount=%u"
                 " evidence=Battle.cbm:sub_6D12_iteminfo_wrapper_and_reader_state\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r4,
                 r5,
                 r9,
                 sp,
                 wrapperCount,
                 wrapperCursor,
                 reader20,
                 reader24,
                 reader28,
                 s_battleActioninfoWrapperTraceCount);

    vm_net_trace_mem("trace_battle_actioninfo_wrapper_stack", sp, 0x80);
    if (r5 != 0)
        vm_net_trace_mem("trace_battle_actioninfo_wrapper_reader", r5, 0x40);
}

static void vm_net_trace_battle_teaminfo_wrapper_detail(const char *label, u32 pc)
{
    static u32 s_battleTeaminfoWrapperTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 teamCount = 0;
    u32 localBase = 0;
    u32 teamLocal = 0;
    u32 slotCountA = 0;
    u32 slotCountB = 0;
    u32 r12v = 0;
    u32 slotId[4] = {0, 0, 0, 0};
    u32 slotLink[4] = {0, 0, 0, 0};
    u32 i = 0;

    if (s_battleTeaminfoWrapperTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    uc_reg_read(MTK, UC_ARM_REG_R12, &r12v);

    localBase = r9 + 0x2918u;
    teamLocal = localBase + 0x9B0u;
    (void)vm_net_trace_read_u32(sp + 0x04u, &teamCount);
    (void)vm_net_trace_read_u8(localBase + 0x0Bu, (u8 *)&slotCountB);
    (void)vm_net_trace_read_u8(localBase + 0x0Cu, (u8 *)&slotCountA);
    for (i = 0; i < 4; ++i)
    {
        u32 slotBase = localBase + i * 0xC4u;
        (void)vm_net_trace_read_u32(slotBase + 0x524u, &slotId[i]);
        (void)vm_net_trace_read_u32(slotBase + 0x540u, &slotLink[i]);
    }

    ++s_battleTeaminfoWrapperTraceCount;
    vm_net_trace("trace_battle_teaminfo_wrapper_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " r0=%08x r1=%08x r4=%08x r5=%08x r6=%08x r7=%08x r12=%08x r9=%08x sp=%08x"
                 " teamCount=%u localBase=%08x teamLocal=%08x ownEnemyCounts=%u,%u"
                 " slotIds=%08x,%08x,%08x,%08x slotLinks=%08x,%08x,%08x,%08x count=%u"
                 " evidence=Battle.cbm:sub_6DBC_teaminfo_wrapper_and_slot_link_window_05188cdc_05188e82\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r4,
                 r5,
                 r6,
                 r7,
                 r12v,
                 r9,
                 sp,
                 teamCount,
                 localBase,
                 teamLocal,
                 slotCountA,
                 slotCountB,
                 slotId[0],
                 slotId[1],
                 slotId[2],
                 slotId[3],
                 slotLink[0],
                 slotLink[1],
                 slotLink[2],
                 slotLink[3],
                 s_battleTeaminfoWrapperTraceCount);

    vm_net_trace_mem("trace_battle_teaminfo_wrapper_stack", sp, 0x80);
    if (r4 != 0)
        vm_net_trace_mem("trace_battle_teaminfo_wrapper_reader", r4, 0x40);
    vm_net_trace_mem("trace_battle_teaminfo_wrapper_local", localBase, 0x80);
    if (r6 != 0)
        vm_net_trace_wt_field_table_detail(label ? label : "teaminfo_packet", r6, pc);
}

static void vm_net_trace_battle_actioninfo_loop_detail(const char *label, u32 pc)
{
    static u32 s_battleActioninfoLoopTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 loopIndex = 0;
    u32 actionCount = 0;
    u32 slotTable = 0;
    u32 localBase = 0;
    u32 activeBlock = 0;
    u8 state0 = 0, state1 = 0, state2 = 0, state3 = 0, state4 = 0, stateE = 0;

    if (s_battleActioninfoLoopTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    (void)vm_net_trace_read_u32(sp + 0x2Cu, &loopIndex);
    (void)vm_net_trace_read_u32(sp + 0x30u, &actionCount);
    (void)vm_net_trace_read_u32(sp + 0x34u, &slotTable);
    localBase = r9 + 0x2918u;
    activeBlock = localBase + 0xC4u * (u32)(state2);
    (void)vm_net_trace_read_u8(localBase + 0x00u, &state0);
    (void)vm_net_trace_read_u8(localBase + 0x01u, &state1);
    (void)vm_net_trace_read_u8(localBase + 0x02u, &state2);
    (void)vm_net_trace_read_u8(localBase + 0x03u, &state3);
    (void)vm_net_trace_read_u8(localBase + 0x04u, &state4);
    (void)vm_net_trace_read_u8(localBase + 0x0Eu, &stateE);
    activeBlock = localBase + 0xC4u * (u32)state2;

    ++s_battleActioninfoLoopTraceCount;
    vm_net_trace("trace_battle_actioninfo_loop_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " r0=%08x r1=%08x r4=%08x r5=%08x r6=%08x r7=%08x r9=%08x sp=%08x"
                 " loopIndex=%u actionCount=%u slotTable=%08x localState=%u,%u,%u,%u,%u,%u activeBlock=%08x count=%u"
                 " evidence=Battle.cbm:sub_6EB0_actionnum_loop_and_return_window_05188f0c_05189104\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r4,
                 r5,
                 r6,
                 r7,
                 r9,
                 sp,
                 loopIndex,
                 actionCount,
                 slotTable,
                 (unsigned int)state0,
                 (unsigned int)state1,
                 (unsigned int)state2,
                 (unsigned int)state3,
                 (unsigned int)state4,
                 (unsigned int)stateE,
                 activeBlock,
                 s_battleActioninfoLoopTraceCount);

    vm_net_trace_mem("trace_battle_actioninfo_loop_stack", sp, 0x50);
    if (slotTable != 0)
        vm_net_trace_mem("trace_battle_actioninfo_loop_slot_table", slotTable, 0x80);
}

static void vm_net_trace_battle_actioninfo_materialize_detail(const char *label, u32 pc)
{
    static u32 s_battleActioninfoMaterializeTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 battleState = 0;
    u32 effectStride = 0;
    u32 effectTable = 0;
    u32 callbackOwner = 0;
    u32 callbackFn = 0;
    u32 effectTemplate = 0;
    u32 recordBlobId = 0;
    u32 recordValueA = 0;
    u32 recordValueB = 0;
    u8 recordType = 0;
    u8 recordActor = 0;
    u8 recordSubCount = 0;
    u8 tail0 = 0, tail1 = 0, tail2 = 0;

    if (s_battleActioninfoMaterializeTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    battleState = r9 + 0x3450u;
    (void)vm_net_trace_read_u16(battleState + 0x18u, (u16 *)&effectStride);
    (void)vm_net_trace_read_u32(battleState + 0x40u, &effectTable);
    (void)vm_net_trace_read_u32(battleState + 0x24u, &callbackOwner);
    if (callbackOwner != 0)
        (void)vm_net_trace_read_u32(callbackOwner + 0x14u, &callbackFn);
    if (r6 != 0)
    {
        (void)vm_net_trace_read_u8(r6 + 1u, &recordType);
        (void)vm_net_trace_read_u8(r6 + 2u, &recordActor);
        (void)vm_net_trace_read_u8(r6 + 3u, &recordSubCount);
        (void)vm_net_trace_read_u32(r6 + 0x18u, &recordValueA);
        (void)vm_net_trace_read_u32(r6 + 0x1Cu, &recordValueB);
        (void)vm_net_trace_read_u32(r6 + 0x5Cu, &recordBlobId);
        (void)vm_net_trace_read_u8(r6 + 0x60u, &tail0);
        (void)vm_net_trace_read_u8(r6 + 0x61u, &tail1);
        (void)vm_net_trace_read_u8(r6 + 0x62u, &tail2);
    }
    if (effectStride != 0 && effectTable != 0)
        effectTemplate = effectTable + effectStride * recordBlobId;

    ++s_battleActioninfoMaterializeTraceCount;
    vm_net_trace("trace_battle_actioninfo_materialize_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " battleState=%08x recordBase=%08x record=%u,%u,%u valueA=%u valueB=%u blobId=%u tail=%u,%u,%u"
                 " effectStride=%u effectTable=%08x effectTemplate=%08x callbackOwner=%08x callbackFn=%08x count=%u"
                 " evidence=Battle.cbm:sub_6EB0_type1_type2_materialize_window_05188efc_0518909a\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 battleState,
                 r6,
                 (unsigned int)recordType,
                 (unsigned int)recordActor,
                 (unsigned int)recordSubCount,
                 recordValueA,
                 recordValueB,
                 (unsigned int)recordBlobId,
                 (unsigned int)tail0,
                 (unsigned int)tail1,
                 (unsigned int)tail2,
                 effectStride,
                 effectTable,
                 effectTemplate,
                 callbackOwner,
                 callbackFn,
                 s_battleActioninfoMaterializeTraceCount);

    if (sp != 0)
        vm_net_trace_mem("trace_battle_actioninfo_materialize_scratch", sp + 0x08u, 0x20);
    if (r6 != 0)
        vm_net_trace_mem("trace_battle_actioninfo_materialize_record", r6, 0x64);
    if (effectTemplate != 0)
        vm_net_trace_mem("trace_battle_actioninfo_materialize_effect_template", effectTemplate, 0x20);
}

static void vm_net_trace_battle_status7_combatinfo_detail(const char *label, u32 pc)
{
    static u32 s_battleStatus7CombatinfoTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 battleState = 0;
    u32 infoBuf = 0;
    u32 fbsBuf = 0;
    u32 fdataBuf = 0;
    u32 streamWord0 = 0;
    u32 streamWord4 = 0;
    u32 streamWord8 = 0;
    u32 streamWordC = 0;
    u32 streamWord10 = 0;
    u32 streamWord14 = 0;
    u32 readerWord0 = 0;
    u32 readerWord4 = 0;
    u32 readerWord8 = 0;
    u32 readerWordC = 0;
    u32 readerWord10 = 0;
    u32 readerWord14 = 0;
    u32 readerCursor = 0;
    u32 readerBlobObj = 0;
    u32 readerBlobPtr = 0;
    u32 readerBlobLen = 0;
    u8 nextBytes[32];
    char nextHex[3 * 16 + 1];
    u32 nextReadLen = 0;
    u8 autoRevive = 0;
    u8 infoGate = 0;

    if (s_battleStatus7CombatinfoTraceCount >= 192)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    battleState = r9 + 0x3450u;
    infoBuf = battleState + 0x30u;
    fbsBuf = battleState + 0x34u;
    fdataBuf = battleState + 0x38u;
    (void)vm_net_trace_read_u8(battleState + 0x0Au, &autoRevive);
    (void)vm_net_trace_read_u8(battleState + 0x0Bu, &infoGate);
    memset(nextBytes, 0, sizeof(nextBytes));
    nextHex[0] = 0;
    if (r4 != 0)
    {
        (void)vm_net_trace_read_u32(r4 + 0x00u, &streamWord0);
        (void)vm_net_trace_read_u32(r4 + 0x04u, &streamWord4);
        (void)vm_net_trace_read_u32(r4 + 0x08u, &streamWord8);
        (void)vm_net_trace_read_u32(r4 + 0x0Cu, &streamWordC);
        (void)vm_net_trace_read_u32(r4 + 0x10u, &streamWord10);
        (void)vm_net_trace_read_u32(r4 + 0x14u, &streamWord14);
    }
    if (r5 != 0)
    {
        (void)vm_net_trace_read_u32(r5 + 0x00u, &readerWord0);
        (void)vm_net_trace_read_u32(r5 + 0x04u, &readerWord4);
        (void)vm_net_trace_read_u32(r5 + 0x08u, &readerWord8);
        (void)vm_net_trace_read_u32(r5 + 0x0Cu, &readerWordC);
        (void)vm_net_trace_read_u32(r5 + 0x10u, &readerWord10);
        (void)vm_net_trace_read_u32(r5 + 0x14u, &readerWord14);
        (void)vm_net_trace_read_u32(r5 + 0x400u, &readerCursor);
        (void)vm_net_trace_read_u32(r5 + 0x408u, &readerBlobObj);
        if (readerBlobObj != 0)
        {
            (void)vm_net_trace_read_u32(readerBlobObj + 0x04u, &readerBlobPtr);
            (void)vm_net_trace_read_u32(readerBlobObj + 0x08u, &readerBlobLen);
        }
    }
    if (readerBlobPtr != 0 && readerCursor < readerBlobLen)
    {
        u32 avail = readerBlobLen - readerCursor;
        nextReadLen = avail < sizeof(nextBytes) ? avail : sizeof(nextBytes);
        if (uc_mem_read(MTK, readerBlobPtr + readerCursor, nextBytes, nextReadLen) == UC_ERR_OK)
            vm_format_trace_bytes_hex(nextBytes, nextReadLen, nextHex, sizeof(nextHex));
        else
            nextReadLen = 0;
    }

    ++s_battleStatus7CombatinfoTraceCount;
    vm_net_trace("trace_battle_status7_combatinfo_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " parserObj=%08x stateObj=%08x battleState=%08x infoBuf=%08x fbsBuf=%08x fdataBuf=%08x"
                 " autoRevive=%u infoGate=%u stream=%08x,%08x,%08x,%08x,%08x,%08x"
                 " reader=%08x,%08x,%08x,%08x,%08x,%08x cursor=%u blobObj=%08x blobPtr=%08x blobLen=%u nextLen=%u next=%s count=%u"
                 " evidence=Battle.cbm:HandleBattleSettleMsg_0x743C_combatinfo_reads_0x78F0_0x79D8\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 r6,
                 r7,
                 battleState,
                 infoBuf,
                 fbsBuf,
                 fdataBuf,
                 (unsigned int)autoRevive,
                 (unsigned int)infoGate,
                 streamWord0,
                 streamWord4,
                 streamWord8,
                 streamWordC,
                 streamWord10,
                 streamWord14,
                 readerWord0,
                 readerWord4,
                 readerWord8,
                 readerWordC,
                 readerWord10,
                 readerWord14,
                 (unsigned int)readerCursor,
                 readerBlobObj,
                 readerBlobPtr,
                 (unsigned int)readerBlobLen,
                 nextReadLen,
                 nextHex,
                 s_battleStatus7CombatinfoTraceCount);

    if (r6 != 0)
    {
        vm_net_trace_wt_field_table_detail(label ? label : "status7_packet", r6, pc);
        vm_net_trace_mem("trace_battle_status7_combatinfo_obj", r6, 96);
    }
    vm_net_trace_mem("trace_battle_status7_info_buf", infoBuf, 32);
    vm_net_trace_mem("trace_battle_status7_fbs_buf", fbsBuf, 32);
    vm_net_trace_mem("trace_battle_status7_fdata_buf", fdataBuf, 32);
    if (r4 != 0)
        vm_net_trace_mem("trace_battle_status7_combatinfo_streamobj", r4, 96);
    if (r5 != 0)
        vm_net_trace_mem("trace_battle_status7_combatinfo_reader", r5, 96);
    if (readerBlobObj != 0)
        vm_net_trace_mem("trace_battle_status7_combatinfo_blobobj", readerBlobObj, 32);
    if (readerBlobPtr != 0)
        vm_net_trace_mem("trace_battle_status7_combatinfo_blob", readerBlobPtr, 96);
}

static void vm_net_trace_battle_local_action_state(const char *label, u32 pc)
{
    static u32 s_battleLocalActionStateTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 localBase = 0;
    u32 activeSlot = 0;
    u32 activeBlock = 0;
    u32 activeActionBlock = 0;
    u32 sharedActionBase = 0;
    u32 actionValueA32 = 0;
    u32 actionValueB32 = 0;
    u32 actionTargetId = 0;
    u8 state0 = 0, state1 = 0, state2 = 0, state3 = 0, state4 = 0, state5 = 0, stateE = 0;
    u8 actionA = 0, actionB = 0, actionC = 0, actionD = 0, actionE = 0, actionF = 0;
    u8 activeActionType = 0;
    u8 activeActionByte5 = 0;
    u8 activeActionByte6 = 0;
    u8 activeActionByte8 = 0;

    if (s_battleLocalActionStateTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    localBase = r9 + 0x2918u;
    sharedActionBase = localBase + 0x0Cu;
    (void)vm_net_trace_read_u8(localBase + 0x00u, &state0);
    (void)vm_net_trace_read_u8(localBase + 0x01u, &state1);
    (void)vm_net_trace_read_u8(localBase + 0x02u, &state2);
    (void)vm_net_trace_read_u8(localBase + 0x03u, &state3);
    (void)vm_net_trace_read_u8(localBase + 0x04u, &state4);
    (void)vm_net_trace_read_u8(localBase + 0x05u, &state5);
    (void)vm_net_trace_read_u8(localBase + 0x0Eu, &stateE);
    activeSlot = (u32)state2;
    activeBlock = localBase + activeSlot * 0xC4u;
    activeActionBlock = activeBlock + 0x520u;
    (void)vm_net_trace_read_u8(activeActionBlock + 0x04u, &activeActionType);
    (void)vm_net_trace_read_u8(activeActionBlock + 0x05u, &activeActionByte5);
    (void)vm_net_trace_read_u8(activeActionBlock + 0x06u, &activeActionByte6);
    (void)vm_net_trace_read_u8(activeActionBlock + 0x08u, &activeActionByte8);
    (void)vm_net_trace_read_u32(activeActionBlock + 0x0Cu, &actionValueA32);
    (void)vm_net_trace_read_u32(activeActionBlock + 0x10u, &actionValueB32);
    (void)vm_net_trace_read_u32(activeActionBlock + 0x24u, &actionTargetId);

    (void)vm_net_trace_read_u8(r9 + 0x345Au, &actionA);
    (void)vm_net_trace_read_u8(r9 + 0x345Bu, &actionB);
    (void)vm_net_trace_read_u8(r9 + 0x345Cu, &actionC);
    (void)vm_net_trace_read_u8(r9 + 0x345Du, &actionD);
    (void)vm_net_trace_read_u8(r9 + 0x345Eu, &actionE);
    (void)vm_net_trace_read_u8(r9 + 0x345Fu, &actionF);

    ++s_battleLocalActionStateTraceCount;
    vm_net_trace("trace_battle_local_action_state label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " localBase=%08x sharedActionBase=%08x activeSlot=%u activeBlock=%08x activeActionBlock=%08x"
                 " localState=%u,%u,%u,%u,%u,%u,%u actionBytes=%u,%u,%u,%u,%u,%u"
                 " activeAction=%u,%u,%u,%u valueAB=%u,%u targetId=%u count=%u"
                 " evidence=Battle.cbm:local_action_state_consumers_051845ba_05184b70\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 localBase,
                 sharedActionBase,
                 activeSlot,
                 activeBlock,
                 activeActionBlock,
                 (unsigned int)state0,
                 (unsigned int)state1,
                 (unsigned int)state2,
                 (unsigned int)state3,
                 (unsigned int)state4,
                 (unsigned int)state5,
                 (unsigned int)stateE,
                 (unsigned int)actionA,
                 (unsigned int)actionB,
                 (unsigned int)actionC,
                 (unsigned int)actionD,
                 (unsigned int)actionE,
                 (unsigned int)actionF,
                 (unsigned int)activeActionType,
                 (unsigned int)activeActionByte5,
                 (unsigned int)activeActionByte6,
                 (unsigned int)activeActionByte8,
                 actionValueA32,
                 actionValueB32,
                 actionTargetId,
                 s_battleLocalActionStateTraceCount);

    vm_net_trace_mem("trace_battle_local_action_head", localBase, 0x40);
    vm_net_trace_mem("trace_battle_local_action_table", sharedActionBase, 0x80);
    vm_net_trace_mem("trace_battle_local_action_active_block", activeBlock, 0x80);
    vm_net_trace_mem("trace_battle_local_action_active_action_block", activeActionBlock, 0x40);
}

static void vm_net_trace_battle_damage_dispatch_detail(const char *label, u32 pc)
{
    static u32 s_battleDamageDispatchTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 localBase = 0;
    u32 activeSlot = 0;
    u32 activeBlock = 0;
    u32 activeActionBlock = 0;
    u32 cbFromR1 = 0;
    u32 state2cFromR1PlusR6 = 0;
    u32 state2cFromR0PlusR6 = 0;
    u8 activeSlotByte = 0;

    if (s_battleDamageDispatchTraceCount >= 64)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    localBase = r9 + 0x2918u;
    (void)vm_net_trace_read_u8(localBase + 0x02u, &activeSlotByte);
    activeSlot = (u32)activeSlotByte;
    activeBlock = localBase + activeSlot * 0xC4u;
    activeActionBlock = activeBlock + 0x520u;
    if (r1 != 0)
        (void)vm_net_trace_read_u32(r1 + 0x0Cu, &cbFromR1);
    if (r1 != 0)
        (void)vm_net_trace_read_u32(r1 + r6 + 0x2Cu, &state2cFromR1PlusR6);
    if (r0 != 0)
        (void)vm_net_trace_read_u32(r0 + r6 + 0x2Cu, &state2cFromR0PlusR6);

    ++s_battleDamageDispatchTraceCount;
    vm_net_trace("trace_battle_damage_dispatch_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " localBase=%08x activeSlot=%u activeBlock=%08x activeActionBlock=%08x"
                 " r0Arg=%08x r1Arg=%08x r2Arg=%08x r5=%08x r6=%08x r7=%08x"
                 " cbFromR1=%08x state2cR1PlusR6=%08x state2cR0PlusR6=%08x count=%u"
                 " evidence=Battle.cbm:damage_dispatch_windows_05186c88_05186cea\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 localBase,
                 activeSlot,
                 activeBlock,
                 activeActionBlock,
                 r0,
                 r1,
                 r2,
                 r5,
                 r6,
                 r7,
                 cbFromR1,
                 state2cFromR1PlusR6,
                 state2cFromR0PlusR6,
                 s_battleDamageDispatchTraceCount);

    if (r0 != 0)
        vm_net_trace_mem("trace_battle_damage_dispatch_arg0", r0, 0x40);
    if (r1 != 0)
        vm_net_trace_mem("trace_battle_damage_dispatch_arg1", r1, 0x40);
    if (activeActionBlock != 0)
        vm_net_trace_mem("trace_battle_damage_dispatch_active_action_block", activeActionBlock, 0x40);
}

static void vm_net_trace_battle_anim_effect_delta_detail(const char *label, u32 pc)
{
    static u32 s_battleAnimEffectDeltaTraceCount = 0;
    static u32 s_battleAnimEffectDeltaDamageCount = 0;
    static u32 s_battleAnimEffectDeltaState4Count = 0;
    static u32 s_battleAnimEffectDeltaHighValueCount = 0;
    u32 *selectedCount = &s_battleAnimEffectDeltaHighValueCount;
    u32 selectedLimit = 192;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 localBase = 0;
    u32 activeSlot = 0;
    u32 activeBlock = 0;
    u32 activeActionBlock = 0;
    u32 fighterBase = 0;
    u32 actionValueA32 = 0;
    u32 actionValueB32 = 0;
    u32 actionTargetId = 0;
    u32 ownHp = 0, ownMaxHp = 0, enemyHp = 0, enemyMaxHp = 0;
    u32 scratch18 = 0, scratch1C = 0, scratch78 = 0, scratch7C = 0;
    u16 pendingDeltaA = 0, pendingDeltaB = 0;
    u8 activeSlot8 = 0;
    u8 local0d = 0, local0e = 0;
    u8 actionType = 0, actionByte5 = 0, actionByte6 = 0, actionByte8 = 0, actionFlagA = 0, actionFlagB = 0;

    if (label && label[0] == 'd')
    {
        selectedCount = &s_battleAnimEffectDeltaDamageCount;
        selectedLimit = 24;
    }
    else if (label && label[0] == 's')
    {
        selectedCount = &s_battleAnimEffectDeltaState4Count;
        selectedLimit = 128;
    }

    if (*selectedCount >= selectedLimit)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    if (r9 == 0)
        return;

    localBase = r9 + 0x2918u;
    fighterBase = r9 + 0x374Cu;
    (void)vm_net_trace_read_u8(localBase + 0x02u, &activeSlot8);
    (void)vm_net_trace_read_u8(localBase + 0x0Du, &local0d);
    (void)vm_net_trace_read_u8(localBase + 0x0Eu, &local0e);
    activeSlot = (u32)activeSlot8;
    activeBlock = localBase + activeSlot * 0xC4u;
    activeActionBlock = activeBlock + 0x520u;

    (void)vm_net_trace_read_u8(activeActionBlock + 0x04u, &actionType);
    (void)vm_net_trace_read_u8(activeActionBlock + 0x05u, &actionByte5);
    (void)vm_net_trace_read_u8(activeActionBlock + 0x06u, &actionByte6);
    (void)vm_net_trace_read_u8(activeActionBlock + 0x08u, &actionByte8);
    (void)vm_net_trace_read_u8(activeActionBlock + 0x0Au, &actionFlagA);
    (void)vm_net_trace_read_u8(activeActionBlock + 0x0Bu, &actionFlagB);
    (void)vm_net_trace_read_u32(activeActionBlock + 0x10u, &actionValueA32);
    (void)vm_net_trace_read_u32(activeActionBlock + 0x18u, &actionValueB32);
    (void)vm_net_trace_read_u32(activeActionBlock + 0x24u, &actionTargetId);
    (void)vm_net_trace_read_u16(r9 + 0x34B4u, &pendingDeltaA);
    (void)vm_net_trace_read_u16(r9 + 0x34B6u, &pendingDeltaB);
    (void)vm_net_trace_read_u32(fighterBase + 0x10u, &ownHp);
    (void)vm_net_trace_read_u32(fighterBase + 0x14u, &ownMaxHp);
    (void)vm_net_trace_read_u32(fighterBase + 0xC4u + 0x10u, &enemyHp);
    (void)vm_net_trace_read_u32(fighterBase + 0xC4u + 0x14u, &enemyMaxHp);
    if (sp != 0)
    {
        (void)vm_net_trace_read_u32(sp + 0x618u, &scratch18);
        (void)vm_net_trace_read_u32(sp + 0x61Cu, &scratch1C);
        (void)vm_net_trace_read_u32(sp + 0x678u, &scratch78);
        (void)vm_net_trace_read_u32(sp + 0x67Cu, &scratch7C);
    }

    ++(*selectedCount);
    ++s_battleAnimEffectDeltaTraceCount;
    vm_net_trace("trace_battle_anim_effect_delta_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " localBase=%08x local0d=%u local0e=%u activeSlot=%u activeBlock=%08x activeActionBlock=%08x"
                 " action=%u,%u,%u,%u flags=%u,%u valueAB=%u,%u targetId=%u pendingDelta=%d,%d"
                 " ownHp=%u/%u enemyHp=%u/%u scratch=%08x,%08x,%08x,%08x count=%u labelCount=%u"
                 " evidence=Battle.cbm:DrawBattleAnimEffect_state_windows_and_sub_4B38_pending_delta_consumer\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 localBase,
                 (unsigned int)local0d,
                 (unsigned int)local0e,
                 activeSlot,
                 activeBlock,
                 activeActionBlock,
                 (unsigned int)actionType,
                 (unsigned int)actionByte5,
                 (unsigned int)actionByte6,
                 (unsigned int)actionByte8,
                 (unsigned int)actionFlagA,
                 (unsigned int)actionFlagB,
                 actionValueA32,
                 actionValueB32,
                 actionTargetId,
                 (int)(short)pendingDeltaA,
                 (int)(short)pendingDeltaB,
                 ownHp,
                 ownMaxHp,
                 enemyHp,
                 enemyMaxHp,
                 scratch18,
                 scratch1C,
                 scratch78,
                 scratch7C,
                 s_battleAnimEffectDeltaTraceCount,
                 *selectedCount);
}

static void vm_net_trace_battle_state4_detail(const char *label, u32 pc)
{
    static u32 s_battleState4DetailTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 localBase = 0;
    u32 activeSlot = 0;
    u32 currentBlock = 0;
    u32 activeActionBlock = 0;
    u32 state4cc = 0;
    u32 callback52c = 0;
    u32 copiedTarget44 = 0;
    u8 activeSlot8 = 0;
    u8 local0d = 0;
    u8 local0e = 0;
    u8 flag52a = 0;
    u8 flag52b = 0;

    if (s_battleState4DetailTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    localBase = r9 + 0x2918u;
    (void)vm_net_trace_read_u8(localBase + 0x02u, &activeSlot8);
    activeSlot = (u32)activeSlot8;
    currentBlock = localBase + activeSlot * 0xC4u;
    activeActionBlock = currentBlock + 0x520u;

    if (r4 != 0)
    {
        (void)vm_net_trace_read_u8(r4 + 0x0Du, &local0d);
        (void)vm_net_trace_read_u8(r4 + 0x0Eu, &local0e);
        (void)vm_net_trace_read_u32(r4 + 0x44u, &copiedTarget44);
    }
    (void)vm_net_trace_read_u32(currentBlock + 0x4CCu, &state4cc);
    (void)vm_net_trace_read_u8(activeActionBlock + 0x0Au, &flag52a);
    (void)vm_net_trace_read_u8(activeActionBlock + 0x0Bu, &flag52b);
    (void)vm_net_trace_read_u32(activeActionBlock + 0x0Cu, &callback52c);

    ++s_battleState4DetailTraceCount;
    vm_net_trace("trace_battle_state4_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " localBase=%08x activeSlot=%u currentBlock=%08x activeActionBlock=%08x"
                 " local0d=%u local0e=%u state4cc=%u flag52a=%u flag52b=%u callback52c=%08x copiedTarget44=%08x"
                 " count=%u evidence=Battle.cbm:sub_6b08_state4_store_target_branch_05186b44_05186c08\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 localBase,
                 activeSlot,
                 currentBlock,
                 activeActionBlock,
                 (unsigned int)local0d,
                 (unsigned int)local0e,
                 state4cc,
                 (unsigned int)flag52a,
                 (unsigned int)flag52b,
                 callback52c,
                 copiedTarget44,
                 s_battleState4DetailTraceCount);

    if (r4 != 0)
        vm_net_trace_mem("trace_battle_state4_local_record", r4, 0x60);
    vm_net_trace_mem("trace_battle_state4_current_block", currentBlock, 0x60);
    vm_net_trace_mem("trace_battle_state4_active_action_block", activeActionBlock, 0x40);
}

static void vm_net_trace_battle_status7_item_record_detail(const char *label, u32 pc)
{
    static u32 s_battleStatus7ItemRecordTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 loopIndex = 0;
    u32 loopLimit = 0;
    u32 recordDesc = 0;
    u32 slotBase = 0;
    u32 slot8 = 0;
    u32 slotC = 0;
    u32 slot64 = 0;
    u32 slot68 = 0;
    u32 fn1c = 0;
    u32 fn20 = 0;
    u32 fn24 = 0;
    u32 fn28 = 0;
    u8 slotIndex = 0;
    u8 slot9e = 0;
    u8 slot9f = 0;

    if (s_battleStatus7ItemRecordTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    (void)vm_net_trace_read_u32(sp + 0x16Cu, &loopIndex);
    (void)vm_net_trace_read_u32(sp + 0x168u, &loopLimit);
    (void)vm_net_trace_read_u32(sp + 0x170u, &recordDesc);
    if (r5 != 0)
    {
        (void)vm_net_trace_read_u32(r5 + 0x1Cu, &fn1c);
        (void)vm_net_trace_read_u32(r5 + 0x20u, &fn20);
        (void)vm_net_trace_read_u32(r5 + 0x24u, &fn24);
        (void)vm_net_trace_read_u32(r5 + 0x28u, &fn28);
    }
    if (recordDesc != 0)
    {
        (void)vm_net_trace_read_u8(recordDesc + 0x0Au, &slotIndex);
        slotBase = r9 + 0x3D6Cu + (u32)slotIndex * 0x40u;
        (void)vm_net_trace_read_u32(slotBase + 0x08u, &slot8);
        (void)vm_net_trace_read_u32(slotBase + 0x0Cu, &slotC);
        (void)vm_net_trace_read_u32(slotBase + 0x64u, &slot64);
        (void)vm_net_trace_read_u32(slotBase + 0x68u, &slot68);
        (void)vm_net_trace_read_u8(slotBase + 0x9Eu, &slot9e);
        (void)vm_net_trace_read_u8(slotBase + 0x9Fu, &slot9f);
    }

    ++s_battleStatus7ItemRecordTraceCount;
    vm_net_trace("trace_battle_status7_item_record_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " loopIndex=%u loopLimit=%u recordDesc=%08x stream=%08x streamFns=%08x,%08x,%08x,%08x"
                 " slotIndex=%u slotBase=%08x slot8=%08x slotC=%08x slot64=%08x slot68=%08x slot9e=%u slot9f=%u count=%u"
                 " evidence=Battle.cbm:sub_743C_iteminfo_record_loop_0518952a_05189a0c\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 loopIndex,
                 loopLimit,
                 recordDesc,
                 r4,
                 fn1c,
                 fn20,
                 fn24,
                 fn28,
                 (unsigned int)slotIndex,
                 slotBase,
                 slot8,
                 slotC,
                 slot64,
                 slot68,
                 (unsigned int)slot9e,
                 (unsigned int)slot9f,
                 s_battleStatus7ItemRecordTraceCount);

    if (recordDesc != 0)
        vm_net_trace_mem("trace_battle_status7_item_record_desc", recordDesc, 0x20);
    if (slotBase != 0)
    {
        vm_net_trace_mem("trace_battle_status7_item_record_slot_head", slotBase, 0x80);
        vm_net_trace_mem("trace_battle_status7_item_record_slot_tail", slotBase + 0x80u, 0x30);
    }
    if (r4 != 0)
        vm_net_trace_mem("trace_battle_status7_item_record_stream", r4, 0x60);
}

static void vm_net_trace_battle_status7_sub7228_detail(const char *label, u32 pc)
{
    static u32 s_battleStatus7Sub7228TraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 slot8 = 0;
    u32 slotC = 0;
    u32 slot64 = 0;
    u32 slot68 = 0;
    u32 guardAddr = 0;
    u8 guardByte = 0;
    u8 slot5a = 0;

    if (s_battleStatus7Sub7228TraceCount >= 64)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    if (r0 != 0)
    {
        (void)vm_net_trace_read_u32(r0 + 0x08u, &slot8);
        (void)vm_net_trace_read_u32(r0 + 0x0Cu, &slotC);
        (void)vm_net_trace_read_u32(r0 + 0x64u, &slot64);
        (void)vm_net_trace_read_u32(r0 + 0x68u, &slot68);
        (void)vm_net_trace_read_u8(r0 + 0x5Au, &slot5a);
        guardAddr = r0 + (r1 << 6) + 0x9Eu;
        (void)vm_net_trace_read_u8(guardAddr, &guardByte);
    }

    ++s_battleStatus7Sub7228TraceCount;
    vm_net_trace("trace_battle_status7_sub7228_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " slotBase=%08x slotIndexArg=%u slot8=%08x slotC=%08x slot64=%08x slot68=%08x slot5a=%u guardAddr=%08x guardByte=%u count=%u"
                 " evidence=Battle.cbm:sub_7228_guard_triplet_05189148_05189208\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 r0,
                 r1,
                 slot8,
                 slotC,
                 slot64,
                 slot68,
                 (unsigned int)slot5a,
                 guardAddr,
                 (unsigned int)guardByte,
                 s_battleStatus7Sub7228TraceCount);

    if (r0 != 0)
    {
        vm_net_trace_mem("trace_battle_status7_sub7228_slot_head", r0, 0x80);
        vm_net_trace_mem("trace_battle_status7_sub7228_slot_tail", r0 + 0x80u, 0x30);
    }
}

static void vm_net_trace_battle_apply_detail(const char *label, u32 pc)
{
    static u32 s_battleApplyDetailTraceCount = 0;
    u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r9 = 0;
    u32 localBase = 0;
    u32 activeSlot = 0;
    u32 activeBlock = 0;
    u32 activeStageBlock = 0;
    u32 activeActionBlock = 0;
    u32 stateWindow = 0;
    u16 pendingDeltaA = 0;
    u16 pendingDeltaB = 0;
    u8 activeSlot8 = 0;
    u8 localFlagA = 0;
    u8 localFlagD = 0;
    u8 stateB = 0;
    u8 stateC = 0;
    u8 stageB = 0;
    u8 stageC = 0;

    if (s_battleApplyDetailTraceCount >= 48)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);

    localBase = r9 + 0x2918u;
    (void)vm_net_trace_read_u8(localBase + 0x02u, &activeSlot8);
    activeSlot = (u32)activeSlot8;
    activeBlock = localBase + activeSlot * 0xC4u;
    activeStageBlock = activeBlock + 0x500u;
    activeActionBlock = activeBlock + 0x520u;
    stateWindow = localBase + 0x9B0u;

    (void)vm_net_trace_read_u8(localBase + 0x0Au, &localFlagA);
    (void)vm_net_trace_read_u8(localBase + 0x0Du, &localFlagD);
    (void)vm_net_trace_read_u8(stateWindow + 0x0Bu, &stateB);
    (void)vm_net_trace_read_u8(stateWindow + 0x0Cu, &stateC);
    (void)vm_net_trace_read_u8(activeStageBlock + 0x0Bu, &stageB);
    (void)vm_net_trace_read_u8(activeStageBlock + 0x0Cu, &stageC);
    (void)vm_net_trace_read_u16(r9 + 0x34B4u, &pendingDeltaA);
    (void)vm_net_trace_read_u16(r9 + 0x34B6u, &pendingDeltaB);

    ++s_battleApplyDetailTraceCount;
    vm_net_trace("trace_battle_apply_detail label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x r9=%08x"
                 " localBase=%08x activeSlot=%u activeBlock=%08x activeStageBlock=%08x activeActionBlock=%08x stateWindow=%08x"
                 " flagsLocal=%u,%u flagsState=%u,%u flagsStage=%u,%u pendingDelta=%u,%u count=%u"
                 " evidence=Battle.cbm:sub_4B70_apply_merge_send25_window_05184b70_05184df4\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 r6,
                 r7,
                 sp,
                 r9,
                 localBase,
                 activeSlot,
                 activeBlock,
                 activeStageBlock,
                 activeActionBlock,
                 stateWindow,
                 (unsigned int)localFlagA,
                 (unsigned int)localFlagD,
                 (unsigned int)stateB,
                 (unsigned int)stateC,
                 (unsigned int)stageB,
                 (unsigned int)stageC,
                 (unsigned int)pendingDeltaA,
                 (unsigned int)pendingDeltaB,
                 s_battleApplyDetailTraceCount);

    vm_net_trace_mem("trace_battle_apply_state_window", stateWindow, 0x40);
    vm_net_trace_mem("trace_battle_apply_stage_block", activeStageBlock, 0x80);
    vm_net_trace_mem("trace_battle_apply_action_block", activeActionBlock, 0x80);
}

static void vm_trace_lcd_text(const char *apiName, u32 idx, u32 strPtr, int x, int y, u16 color, const u8 *gbkText)
{
#if TRACE_LCD_TEXT
    if (y < -32 || y > 320)
        return;
    u32 lr = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    gbk_to_utf8((u8 *)gbkText, sprintfBuff, mySizeOf(sprintfBuff));
    int fullWidth = mesureStringWidth((char *)gbkText);
    int curWidth = mesureStringWidthWithGbkWidth((char *)gbkText, getFontWidth());
    int fontApiWidth = (g_currentFontType == 0) ? getFontCellWidth() : getFontWidth();
    vm_net_trace("lcd_text api=%s idx=%u ptr=%08x x=%d y=%d color=%04x fontType=%u fontApiW=%d fullW=%d curW=%d lr=%08x last=%08x text=%s\n",
                 apiName, idx, strPtr, x, y, color, g_currentFontType, fontApiWidth, fullWidth, curWidth, lr, lastAddress, sprintfBuff);
#else
    (void)apiName;
    (void)idx;
    (void)strPtr;
    (void)x;
    (void)y;
    (void)color;
    (void)gbkText;
#endif
}

static void vm_trace_lcd_text_call(const char *apiName, u32 idx, u32 strPtr, u32 r0, u32 r1, u32 r2, u32 r3, u32 sp, int x, int y, u16 color, const u8 *gbkText)
{
#if TRACE_LCD_TEXT
    if (y < -32 || y > 320)
        return;
    u32 lr = 0;
    u32 st[8] = {0};
    u32 savedRegs[9] = {0};
    u32 guestCaller = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, sp, st, sizeof(st));
    uc_mem_read(MTK, sp + 0xCC, savedRegs, sizeof(savedRegs));
    guestCaller = savedRegs[8];
    gbk_to_utf8((u8 *)gbkText, sprintfBuff, mySizeOf(sprintfBuff));
    int fullWidth = mesureStringWidth((char *)gbkText);
    int curWidth = mesureStringWidthWithGbkWidth((char *)gbkText, getFontWidth());
    int fontApiWidth = (g_currentFontType == 0) ? getFontCellWidth() : getFontWidth();
    vm_net_trace("lcd_text_call api=%s idx=%u ptr=%08x x=%d y=%d color=%04x fontType=%u fontApiW=%d fullW=%d curW=%d lr=%08x last=%08x guestCaller=%08x r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x st=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x saved=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x text=%s\n",
                 apiName, idx, strPtr, x, y, color, g_currentFontType, fontApiWidth, fullWidth, curWidth, lr, lastAddress,
                 guestCaller,
                 r0, r1, r2, r3, sp, st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7],
                 savedRegs[0], savedRegs[1], savedRegs[2], savedRegs[3], savedRegs[4], savedRegs[5], savedRegs[6], savedRegs[7], savedRegs[8],
                 sprintfBuff);
#else
    (void)apiName;
    (void)idx;
    (void)strPtr;
    (void)r0;
    (void)r1;
    (void)r2;
    (void)r3;
    (void)sp;
    (void)x;
    (void)y;
    (void)color;
    (void)gbkText;
#endif
}

static void hook_vm_pool_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    static u32 s_battleSub17acFlowCount = 0;
    static u8 s_battleSub17acFlowActive = 0;
    static u32 s_battleSub17acFlowPacket = 0;
    static u32 s_battleSub17acFlowTick = 0;
    static u8 s_battleSub17acFlowKind = 0;
    static u8 s_battleSub17acFlowSubtype = 0;
    static u8 s_battleSub17acExpectNextPcActive = 0;
    static u32 s_battleSub17acExpectNextPcFrom = 0;
    static u32 s_battleKind4Subtype2FlowCount = 0;
    static u8 s_battleKind4Subtype2FlowActive = 0;
    static u32 s_battleKind4Subtype2FlowPacket = 0;
    static u32 s_battleKind4Subtype2FlowTick = 0;
    static u32 s_battleKind4Subtype8FlowCount = 0;
    static u8 s_battleKind4Subtype8FlowActive = 0;
    static u32 s_battleKind4Subtype8FlowPacket = 0;
    static u32 s_battleKind4Subtype8FlowTick = 0;
    static u32 s_battleKind4Subtype6FlowCount = 0;
    static u8 s_battleKind4Subtype6FlowActive = 0;
    static u32 s_battleKind4Subtype6FlowPacket = 0;
    static u32 s_battleKind4Subtype6FlowTick = 0;
    static u32 s_battleKind4Subtype9FlowCount = 0;
    static u8 s_battleKind4Subtype9FlowActive = 0;
    static u32 s_battleKind4Subtype9FlowPacket = 0;
    static u32 s_battleKind4Subtype9FlowTick = 0;
    (void)size;
    (void)user_data;

    u32 tracePc = (u32)address & ~1u;
    u32 traceCodeBase = g_currentScreenModuleBase;
    u32 moduleR9 = g_currentScreenModuleBase;
    u32 inferredCodeBase = 0;
    u32 inferredModuleR9 = 0;
    /*
     * Battle.cbm screen tables give a stable virtual code base for IDA offset
     * mapping, but the dynamic loader chooses the module data/R9 buffer.  In
     * the current logs Battle.cbm code is read into the VM pool while its BSS is
     * read to 01050bd0, so do not derive R9 from codeBase+headerInt2 here.
     */
    u32 loaderModuleR9 = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (loaderModuleR9 == 0)
        loaderModuleR9 = g_currentScreenModuleBase;
    if (tracePc >= 0x05080000 && tracePc < 0x05094000)
    {
        traceCodeBase = 0x05080000;
        moduleR9 = loaderModuleR9;
    }
    else if (tracePc >= 0x05094000 && tracePc < 0x050A8000)
    {
        traceCodeBase = 0x05094000;
        moduleR9 = loaderModuleR9;
    }
    else if (tracePc >= 0x05181F20 && tracePc < 0x05195464)
    {
        /*
         * Latest storage trace shows Battle.cbm code copied to 05181f20
         * while the screen table still reports the virtual base 05174000.
         * Use the guest read buffer for actual executed-code offsets here.
         */
        traceCodeBase = 0x05181F20;
        moduleR9 = loaderModuleR9;
    }
    else if (vm_infer_battle_module_from_screen(vmAddedScreen, &inferredCodeBase, &inferredModuleR9) &&
             tracePc >= inferredCodeBase && tracePc < inferredModuleR9)
    {
        traceCodeBase = inferredCodeBase;
        moduleR9 = loaderModuleR9;
    }
    u32 traceOff = traceCodeBase && tracePc >= traceCodeBase ? tracePc - traceCodeBase : 0xffffffffu;
    u32 currentR9 = 0;
    uc_reg_read(uc, UC_ARM_REG_R9, &currentR9);
    if (moduleR9 && currentR9 != moduleR9)
    {
        if (tracePc >= 0x050175d0 && tracePc < 0x05017620)
            vm_net_trace("pool_r9_fix pc=%08x r9=%08x -> %08x entry=%08x\n",
                         tracePc, currentR9, moduleR9, g_currentEmuEntry);
        if ((traceCodeBase == 0x05080000 && moduleR9 != 0) ||
            (traceCodeBase == 0x05094000 && moduleR9 != 0) ||
            (traceCodeBase == 0x05181F20 && moduleR9 != 0) ||
            (inferredCodeBase != 0 && traceCodeBase == inferredCodeBase && moduleR9 != 0))
        {
            vm_net_trace("pool_r9_fix_battle_loader_r9 pc=%08x off=%04x r9=%08x -> %08x codeBase=%08x last=%08x tick=%u evidence=dynamic_cbm_file_read_guest_write\n",
                         tracePc, traceOff, currentR9, moduleR9, traceCodeBase, lastAddress, g_schedulerTick);
        }
        uc_reg_write(uc, UC_ARM_REG_R9, &moduleR9);
    }

    switch (tracePc)
    {
    case 0x05015B9C:
        vm_net_trace_title_login_site("net_build_login_request_1b9c", tracePc);
        break;
    case 0x05015E5C:
        vm_net_trace_title_login_site("login_form_submit_1e5c", tracePc);
        break;
    case 0x05016A50:
        vm_net_trace_title_login_site("login_alt_response_dispatch_2a50", tracePc);
        break;
    case 0x05016D8E:
        vm_net_trace_title_login_site("login_primary_response_dispatch_2d8e", tracePc);
        break;
    case 0x05016F62:
        vm_net_trace_title_login_site("server_select_request_2f62", tracePc);
        break;
    case 0x05016F92:
        vm_net_trace_title_login_site("server_or_role_list_action_2f92", tracePc);
        break;
    case 0x05017544:
        vm_net_trace_title_login_site("title_rolelist_parser_3544", tracePc);
        break;
    case 0x0501953C:
        vm_net_trace_title_login_site("login_screen_action_553c", tracePc);
        break;
    default:
        break;
    }

    if (s_battleSub17acExpectNextPcActive)
    {
        vm_net_trace("trace_battle_sub17ac_next_pc from=%08x next=%08x off=%04x last=%08x tick=%u active=%u kind=%u subtype=%u evidence=Battle.cbm:post_17f8_actual_next_pc\n",
                     s_battleSub17acExpectNextPcFrom,
                     tracePc,
                     traceOff,
                     lastAddress,
                     g_schedulerTick,
                     (unsigned int)s_battleSub17acFlowActive,
                     (unsigned int)s_battleSub17acFlowKind,
                     (unsigned int)s_battleSub17acFlowSubtype);
        s_battleSub17acExpectNextPcActive = 0;
        s_battleSub17acExpectNextPcFrom = 0;
    }

    if (traceOff == 0x17AC)
    {
        u32 r0 = 0;
        u8 hdr[11] = {0};
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        s_battleSub17acFlowActive = 0;
        s_battleSub17acFlowPacket = 0;
        s_battleSub17acFlowTick = 0;
        s_battleSub17acFlowCount = 0;
        s_battleSub17acFlowKind = 0;
        s_battleSub17acFlowSubtype = 0;
        s_battleSub17acExpectNextPcActive = 0;
        s_battleSub17acExpectNextPcFrom = 0;
        if (r0 != 0 &&
            uc_mem_read(uc, r0, hdr, sizeof(hdr)) == UC_ERR_OK &&
            hdr[0] == 'W' && hdr[1] == 'T' &&
            hdr[4] >= 1 &&
            hdr[5] == 1 &&
            hdr[6] == 25 &&
            (hdr[7] == 2 || hdr[7] == 5 || hdr[7] == 12))
        {
            s_battleSub17acFlowActive = 1;
            s_battleSub17acFlowPacket = r0;
            s_battleSub17acFlowTick = g_schedulerTick;
            s_battleSub17acFlowKind = hdr[6];
            s_battleSub17acFlowSubtype = hdr[7];
            vm_net_trace("trace_battle_sub17ac_flow_start packet=%08x len=%u objectCount=%u kind=%u subtype=%u tick=%u evidence=Battle.cbm:sub_17AC_event7_dispatch_window\n",
                         r0,
                         ((u32)hdr[2] << 8) | (u32)hdr[3],
                         (unsigned int)hdr[4],
                         (unsigned int)s_battleSub17acFlowKind,
                         (unsigned int)s_battleSub17acFlowSubtype,
                         g_schedulerTick);
        }
    }
    else if (!((traceOff >= 0x17AC && traceOff < 0x1850) ||
               (traceOff >= 0x1EE2 && traceOff < 0x1F24) ||
               traceOff == 0x1A9E))
    {
        s_battleSub17acFlowActive = 0;
    }

    if (s_battleSub17acFlowActive &&
        (((traceOff >= 0x17AC && traceOff < 0x1850) ||
          (traceOff >= 0x1EE2 && traceOff < 0x1F24) ||
          traceOff == 0x1A9E)) &&
        s_battleSub17acFlowCount < 320)
    {
        u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, lr = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(uc, UC_ARM_REG_R1, &r1);
        uc_reg_read(uc, UC_ARM_REG_R2, &r2);
        uc_reg_read(uc, UC_ARM_REG_R3, &r3);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        vm_net_trace("trace_battle_sub17ac_flow pc=%08x off=%04x lr=%08x last=%08x tick=%u packet=%08x packetTick=%u kind=%u subtype=%u regs=%08x,%08x,%08x,%08x count=%u evidence=Battle.cbm:sub_17AC_event7_dispatch_window_kind25_family\n",
                     tracePc, traceOff, lr, lastAddress, g_schedulerTick,
                     s_battleSub17acFlowPacket, s_battleSub17acFlowTick,
                     (unsigned int)s_battleSub17acFlowKind,
                     (unsigned int)s_battleSub17acFlowSubtype,
                     r0, r1, r2, r3, s_battleSub17acFlowCount);
        if (traceOff == 0x17EE || traceOff == 0x17F0 || traceOff == 0x18BA || traceOff == 0x18BC)
        {
            u32 r4 = 0, r6 = 0, r7 = 0, sp = 0;
            u32 ctx2c = 0;
            uc_reg_read(uc, UC_ARM_REG_R4, &r4);
            uc_reg_read(uc, UC_ARM_REG_R6, &r6);
            uc_reg_read(uc, UC_ARM_REG_R7, &r7);
            uc_reg_read(uc, UC_ARM_REG_SP, &sp);
            if (r6 != 0)
                (void)vm_net_trace_read_u32(r6 + 0x2Cu, &ctx2c);
            vm_net_trace("trace_battle_sub17ac_gate detailOff=%04x pc=%08x lr=%08x last=%08x tick=%u packet=%08x kind=%u subtype=%u"
                         " regs=%08x,%08x,%08x,%08x r4=%08x r6=%08x r7=%08x sp=%08x ctx2c=%08x"
                         " evidence=Battle.cbm:sub_17AC_callback_return_gate_and_nonzero_branch\n",
                         traceOff,
                         tracePc,
                         lr,
                         lastAddress,
                         g_schedulerTick,
                         s_battleSub17acFlowPacket,
                         (unsigned int)s_battleSub17acFlowKind,
                         (unsigned int)s_battleSub17acFlowSubtype,
                         r0,
                         r1,
                         r2,
                         r3,
                         r4,
                         r6,
                         r7,
                         sp,
                         ctx2c);
            if (ctx2c != 0)
                vm_net_trace_shared_event_container("battle_sub17ac_ctx2c", ctx2c);
        }
        if (traceOff == 0x17F8)
        {
            s_battleSub17acExpectNextPcActive = 1;
            s_battleSub17acExpectNextPcFrom = tracePc;
        }
        if (traceOff == 0x1EE2 || traceOff == 0x1EE4 || traceOff == 0x1EEA || traceOff == 0x1EF2)
            vm_net_trace_battle_sub17ac_loop_gate(tracePc, traceOff);
        ++s_battleSub17acFlowCount;
    }

    if (traceOff == 0x7BD0)
    {
        u32 r1 = 0;
        u32 kind = 0;
        u32 subtype = 0;
        uc_reg_read(uc, UC_ARM_REG_R1, &r1);
        s_battleKind4Subtype2FlowActive = 0;
        s_battleKind4Subtype2FlowPacket = 0;
        s_battleKind4Subtype2FlowTick = 0;
        s_battleKind4Subtype2FlowCount = 0;
        s_battleKind4Subtype8FlowActive = 0;
        s_battleKind4Subtype8FlowPacket = 0;
        s_battleKind4Subtype8FlowTick = 0;
        s_battleKind4Subtype8FlowCount = 0;
        s_battleKind4Subtype6FlowActive = 0;
        s_battleKind4Subtype6FlowPacket = 0;
        s_battleKind4Subtype6FlowTick = 0;
        s_battleKind4Subtype6FlowCount = 0;
        s_battleKind4Subtype9FlowActive = 0;
        s_battleKind4Subtype9FlowPacket = 0;
        s_battleKind4Subtype9FlowTick = 0;
        s_battleKind4Subtype9FlowCount = 0;
        (void)vm_net_trace_read_u32(r1 + 4, &kind);
        (void)vm_net_trace_read_u32(r1 + 8, &subtype);
        if (kind == 4 && subtype == 2)
        {
            s_battleKind4Subtype2FlowActive = 1;
            s_battleKind4Subtype2FlowPacket = r1;
            s_battleKind4Subtype2FlowTick = g_schedulerTick;
            vm_net_trace("trace_battle_kind4_subtype2_flow_start packet=%08x tick=%u evidence=Battle.cbm:sub_7BD0_kind4_subtype2_result_dispatch\n",
                         r1,
                         g_schedulerTick);
        }
        else if (kind == 4 && subtype == 8)
        {
            s_battleKind4Subtype8FlowActive = 1;
            s_battleKind4Subtype8FlowPacket = r1;
            s_battleKind4Subtype8FlowTick = g_schedulerTick;
            vm_net_trace("trace_battle_kind4_subtype8_flow_start packet=%08x tick=%u evidence=Battle.cbm:sub_7BD0_top_kind4_table_subtype8_result_info_parser_05189d16\n",
                         r1,
                         g_schedulerTick);
        }
        else if (kind == 4 && subtype == 6)
        {
            s_battleKind4Subtype6FlowActive = 1;
            s_battleKind4Subtype6FlowPacket = r1;
            s_battleKind4Subtype6FlowTick = g_schedulerTick;
            vm_net_trace("trace_battle_kind4_subtype6_flow_start packet=%08x tick=%u evidence=Battle.cbm:sub_7BD0_top_kind4_table_subtype6_actioninfo_import_05189e84_0518909c\n",
                         r1,
                         g_schedulerTick);
        }
        else if (kind == 4 && subtype == 9)
        {
            s_battleKind4Subtype9FlowActive = 1;
            s_battleKind4Subtype9FlowPacket = r1;
            s_battleKind4Subtype9FlowTick = g_schedulerTick;
            vm_net_trace("trace_battle_kind4_subtype9_flow_start packet=%08x tick=%u evidence=Battle.cbm:sub_7BD0_top_kind4_table_subtype9_result_parser_05189bd2\n",
                         r1,
                         g_schedulerTick);
        }
    }
    else if (traceOff < 0x7BD0 || traceOff >= 0x7D60)
    {
        s_battleKind4Subtype2FlowActive = 0;
    }
    if (traceOff < 0x7BD0 || traceOff >= 0x7F20)
    {
        s_battleKind4Subtype8FlowActive = 0;
    }
    if (traceOff < 0x7BD0 || traceOff >= 0x8000)
    {
        s_battleKind4Subtype9FlowActive = 0;
    }

    if (s_battleKind4Subtype2FlowActive && traceOff >= 0x7BD0 && traceOff < 0x7D60 && s_battleKind4Subtype2FlowCount < 180)
    {
        u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, lr = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(uc, UC_ARM_REG_R1, &r1);
        uc_reg_read(uc, UC_ARM_REG_R2, &r2);
        uc_reg_read(uc, UC_ARM_REG_R3, &r3);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        vm_net_trace("trace_battle_kind4_subtype2_flow pc=%08x off=%04x lr=%08x last=%08x tick=%u packet=%08x packetTick=%u regs=%08x,%08x,%08x,%08x count=%u evidence=Battle.cbm:sub_7BD0_kind4_subtype2_result_dispatch\n",
                     tracePc,
                     traceOff,
                     lr,
                     lastAddress,
                     g_schedulerTick,
                     s_battleKind4Subtype2FlowPacket,
                     s_battleKind4Subtype2FlowTick,
                     r0, r1, r2, r3,
                     s_battleKind4Subtype2FlowCount);
        ++s_battleKind4Subtype2FlowCount;
    }

    if (s_battleKind4Subtype8FlowActive && traceOff >= 0x7BD0 && traceOff < 0x7F20 && s_battleKind4Subtype8FlowCount < 260)
    {
        u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, r5 = 0, r6 = 0, r7 = 0, lr = 0, sp = 0;
        u32 stackInfoPtr = 0;
        u32 parserObj = 0;
        u32 parserBuf = 0;
        u32 parserBufCap = 0;
        u32 parserState = 0;
        u32 battleState = 0;
        u32 infoDstBuf = 0;
        u32 infoDstDumpLen = 32;
        u8 actionReady = 0, actionApplied = 0, actionFlagC = 0, actionFlagD = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(uc, UC_ARM_REG_R1, &r1);
        uc_reg_read(uc, UC_ARM_REG_R2, &r2);
        uc_reg_read(uc, UC_ARM_REG_R3, &r3);
        uc_reg_read(uc, UC_ARM_REG_R5, &r5);
        uc_reg_read(uc, UC_ARM_REG_R6, &r6);
        uc_reg_read(uc, UC_ARM_REG_R7, &r7);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        uc_reg_read(uc, UC_ARM_REG_SP, &sp);
        (void)vm_net_trace_read_u32(sp + 8u, &stackInfoPtr);
        if (vm_net_trace_read_u32(r5, &parserObj) && parserObj != 0)
        {
            (void)vm_net_trace_read_u32(parserObj + 0x30u, &parserBuf);
            (void)vm_net_trace_read_u32(parserObj + 0x34u, &parserBufCap);
            (void)vm_net_trace_read_u32(parserObj + 0x5Cu, &parserState);
        }
        battleState = moduleR9 + 0x3450u;
        (void)vm_net_trace_read_u32(battleState + 0x30u, &infoDstBuf);
        if (stackInfoPtr != 0)
            infoDstDumpLen = 16;
        (void)vm_net_trace_read_u8(battleState + 0x0Au, &actionReady);
        (void)vm_net_trace_read_u8(battleState + 0x0Bu, &actionApplied);
        (void)vm_net_trace_read_u8(battleState + 0x0Cu, &actionFlagC);
        (void)vm_net_trace_read_u8(battleState + 0x0Du, &actionFlagD);
        vm_net_trace("trace_battle_kind4_subtype8_flow pc=%08x off=%04x lr=%08x last=%08x tick=%u packet=%08x packetTick=%u regs=%08x,%08x,%08x,%08x r5=%08x r6=%08x r7=%08x sp=%08x stackInfoPtr=%08x parserObj=%08x parserBuf=%08x parserBufCap=%08x parserState=%08x battleState=%08x infoDstBuf=%08x actionBytes=%u,%u,%u,%u count=%u evidence=Battle.cbm:sub_7BD0_subtype8_info_copy_gate_apply_window_05189d16_05189e20\n",
                     tracePc,
                     traceOff,
                     lr,
                     lastAddress,
                     g_schedulerTick,
                     s_battleKind4Subtype8FlowPacket,
                     s_battleKind4Subtype8FlowTick,
                     r0, r1, r2, r3,
                     r5,
                     r6,
                     r7,
                     sp,
                     stackInfoPtr,
                     parserObj,
                     parserBuf,
                     parserBufCap,
                     parserState,
                     battleState,
                     infoDstBuf,
                     (unsigned int)actionReady,
                     (unsigned int)actionApplied,
                     (unsigned int)actionFlagC,
                     (unsigned int)actionFlagD,
                     s_battleKind4Subtype8FlowCount);
        if (traceOff == 0x7E58 || traceOff == 0x7E62 || traceOff == 0x7E94 || traceOff == 0x7DBC || traceOff == 0x7E20)
        {
            if (r1 != 0 && r2 != 0 && r2 < 0x400u)
                vm_net_trace_mem("trace_battle_kind4_subtype8_flow_src", r1, r2);
            else if (stackInfoPtr != 0 && r7 != 0 && r7 < 0x400u)
                vm_net_trace_mem("trace_battle_kind4_subtype8_flow_stack_src", stackInfoPtr, r7);
            if (stackInfoPtr != 0)
                vm_net_trace_mem("trace_battle_kind4_subtype8_flow_info_src_wrapper", stackInfoPtr, 32);
            if (parserObj != 0)
                vm_net_trace_mem("trace_battle_kind4_subtype8_flow_obj", parserObj, 96);
            if (parserBuf != 0)
                vm_net_trace_mem("trace_battle_kind4_subtype8_flow_dst", parserBuf, 96);
            if (infoDstBuf != 0)
                vm_net_trace_mem("trace_battle_kind4_subtype8_flow_info_dst", infoDstBuf, infoDstDumpLen);
        }
        ++s_battleKind4Subtype8FlowCount;
    }

    if (s_battleKind4Subtype6FlowActive && traceOff >= 0x7F64 && traceOff < 0x7F88 && s_battleKind4Subtype6FlowCount < 320)
    {
        u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r6 = 0, r7 = 0, lr = 0, sp = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(uc, UC_ARM_REG_R1, &r1);
        uc_reg_read(uc, UC_ARM_REG_R2, &r2);
        uc_reg_read(uc, UC_ARM_REG_R3, &r3);
        uc_reg_read(uc, UC_ARM_REG_R4, &r4);
        uc_reg_read(uc, UC_ARM_REG_R6, &r6);
        uc_reg_read(uc, UC_ARM_REG_R7, &r7);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        uc_reg_read(uc, UC_ARM_REG_SP, &sp);
        vm_net_trace("trace_battle_kind4_subtype6_bridge pc=%08x off=%04x lr=%08x last=%08x tick=%u packet=%08x packetTick=%u regs=%08x,%08x,%08x,%08x r4=%08x r6=%08x r7=%08x sp=%08x count=%u evidence=Battle.cbm:sub_7BD0_case6_bridge_into_sub_6EB0_0x7f64_0x7f84\n",
                     tracePc,
                     traceOff,
                     lr,
                     lastAddress,
                     g_schedulerTick,
                     s_battleKind4Subtype6FlowPacket,
                     s_battleKind4Subtype6FlowTick,
                     r0, r1, r2, r3,
                     r4,
                     r6,
                     r7,
                     sp,
                     s_battleKind4Subtype6FlowCount);
        ++s_battleKind4Subtype6FlowCount;
    }

    if (s_battleKind4Subtype6FlowActive && traceOff >= 0x6EB0 && traceOff < 0x7104 && s_battleKind4Subtype6FlowCount < 320)
    {
        u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, lr = 0, sp = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(uc, UC_ARM_REG_R1, &r1);
        uc_reg_read(uc, UC_ARM_REG_R2, &r2);
        uc_reg_read(uc, UC_ARM_REG_R3, &r3);
        uc_reg_read(uc, UC_ARM_REG_R4, &r4);
        uc_reg_read(uc, UC_ARM_REG_R5, &r5);
        uc_reg_read(uc, UC_ARM_REG_R6, &r6);
        uc_reg_read(uc, UC_ARM_REG_R7, &r7);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        uc_reg_read(uc, UC_ARM_REG_SP, &sp);
        vm_net_trace("trace_battle_kind4_subtype6_flow pc=%08x off=%04x lr=%08x last=%08x tick=%u packet=%08x packetTick=%u regs=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x count=%u evidence=Battle.cbm:sub_7BD0_subtype6_actioninfo_import_and_type1_type2_materialize_window_05188dd0_0518909c\n",
                     tracePc,
                     traceOff,
                     lr,
                     lastAddress,
                     g_schedulerTick,
                     s_battleKind4Subtype6FlowPacket,
                     s_battleKind4Subtype6FlowTick,
                     r0, r1, r2, r3, r4, r5, r6, r7,
                     sp,
                     s_battleKind4Subtype6FlowCount);
        ++s_battleKind4Subtype6FlowCount;
    }

    if (s_battleKind4Subtype9FlowActive && traceOff >= 0x7BD0 && traceOff < 0x8000 && s_battleKind4Subtype9FlowCount < 260)
    {
        u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, r5 = 0, lr = 0;
        u32 battleObj = 0;
        u32 gateBase = 0;
        u8 gate0 = 0, gate1 = 0, gate2 = 0, gate3 = 0, gate4 = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(uc, UC_ARM_REG_R1, &r1);
        uc_reg_read(uc, UC_ARM_REG_R2, &r2);
        uc_reg_read(uc, UC_ARM_REG_R3, &r3);
        uc_reg_read(uc, UC_ARM_REG_R5, &r5);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        (void)vm_net_trace_read_u32(r5, &battleObj);
        if (battleObj != 0)
        {
            gateBase = battleObj + 0x470u;
            (void)vm_net_trace_read_u8(gateBase + 0u, &gate0);
            (void)vm_net_trace_read_u8(gateBase + 1u, &gate1);
            (void)vm_net_trace_read_u8(gateBase + 2u, &gate2);
            (void)vm_net_trace_read_u8(gateBase + 3u, &gate3);
            (void)vm_net_trace_read_u8(gateBase + 4u, &gate4);
        }
        vm_net_trace("trace_battle_kind4_subtype9_flow pc=%08x off=%04x lr=%08x last=%08x tick=%u packet=%08x packetTick=%u regs=%08x,%08x,%08x,%08x r5=%08x battleObj=%08x gateBase=%08x gateBytes=%u,%u,%u,%u,%u count=%u evidence=Battle.cbm:sub_7BD0_subtype9_result1_gate_05189bf0\n",
                     tracePc,
                     traceOff,
                     lr,
                     lastAddress,
                     g_schedulerTick,
                     s_battleKind4Subtype9FlowPacket,
                     s_battleKind4Subtype9FlowTick,
                     r0, r1, r2, r3,
                     r5,
                     battleObj,
                     gateBase,
                     (unsigned int)gate0,
                     (unsigned int)gate1,
                     (unsigned int)gate2,
                     (unsigned int)gate3,
                     (unsigned int)gate4,
                     s_battleKind4Subtype9FlowCount);
        ++s_battleKind4Subtype9FlowCount;
    }

    switch (traceOff)
    {
    case 0x6462:
        vm_net_trace_battle_create_charlist_source("create_charlist_full_table_method_load", tracePc, traceCodeBase);
        break;
    case 0x6464:
        vm_net_trace_battle_create_charlist_source("create_charlist_full_table_method_call", tracePc, traceCodeBase);
        break;
    case 0x646A:
        vm_net_trace_battle_create_charlist_source("create_charlist_full_table_return_before_store", tracePc, traceCodeBase);
        break;
    case 0x646C:
        vm_net_trace_battle_create_charlist_source("create_charlist_full_table_after_store", tracePc, traceCodeBase);
        break;
    case 0x65A8:
        vm_net_trace_battle_create_charlist_source("create_charlist_compact_table_method_owner", tracePc, traceCodeBase);
        break;
    case 0x65AA:
        vm_net_trace_battle_create_charlist_source("create_charlist_compact_table_method_load", tracePc, traceCodeBase);
        break;
    case 0x65AC:
        vm_net_trace_battle_create_charlist_source("create_charlist_compact_table_method_call", tracePc, traceCodeBase);
        break;
    case 0x65AE:
        vm_net_trace_battle_create_charlist_source("create_charlist_compact_table_return_before_store", tracePc, traceCodeBase);
        break;
    case 0x65B0:
        vm_net_trace_battle_create_charlist_source("create_charlist_compact_table_after_store", tracePc, traceCodeBase);
        break;
    case 0x04FE:
        vm_net_trace_battle_outgoing_request_source("battle_send_challenge_4_1_entry", tracePc, traceCodeBase);
        break;
    case 0x0A04:
        vm_net_trace_battle_challenge_source_branch("challenge_direct_id_index_load", tracePc, traceCodeBase);
        break;
    case 0x0A0C:
        vm_net_trace_battle_challenge_source_branch("challenge_direct_call_4_1", tracePc, traceCodeBase);
        break;
    case 0x0A20:
        vm_net_trace_battle_challenge_source_branch("challenge_fallback_method_before", tracePc, traceCodeBase);
        break;
    case 0x0A2A:
        vm_net_trace_battle_challenge_source_branch("challenge_fallback_method_after", tracePc, traceCodeBase);
        break;
    case 0x0A38:
        vm_net_trace_battle_challenge_source_branch("challenge_fallback_outparams_loaded", tracePc, traceCodeBase);
        break;
    case 0x0A3C:
        vm_net_trace_battle_challenge_source_branch("challenge_fallback_call_4_1", tracePc, traceCodeBase);
        break;
    case 0x2B50:
        vm_net_trace_battle_outgoing_request_source("battle_send_operate_4_2_entry", tracePc, traceCodeBase);
        break;
    case 0x57A6:
        vm_net_trace_battle_pool_probe("battle_render_fighter_active_addr_57A6", tracePc, traceCodeBase);
        break;
    case 0x57A8:
        vm_net_trace_battle_pool_probe("battle_render_fighter_active_load_57A8", tracePc, traceCodeBase);
        break;
    case 0x57BE:
        vm_net_trace_battle_pool_probe("battle_render_fighter_frame_bytes_57BE", tracePc, traceCodeBase);
        break;
    case 0x57CE:
        vm_net_trace_battle_pool_probe("battle_render_fighter_cb4_load_57CE", tracePc, traceCodeBase);
        break;
    case 0x57D2:
        vm_net_trace_battle_pool_probe("battle_render_fighter_cb4_call_57D2", tracePc, traceCodeBase);
        break;
    case 0x57DC:
        vm_net_trace_battle_pool_probe("battle_render_fighter_cb8_load_57DC", tracePc, traceCodeBase);
        break;
    case 0x57DE:
        vm_net_trace_battle_pool_probe("battle_render_fighter_cb8_call_57DE", tracePc, traceCodeBase);
        break;
    case 0x68FE:
        vm_net_trace_battle_pool_probe("sub_66CC_own_visual_store_actual_buffer", tracePc, traceCodeBase);
        break;
    case 0x6900:
        vm_net_trace_battle_pool_probe("sub_66CC_own_visual_init_callback_actual_buffer", tracePc, traceCodeBase);
        break;
    case 0x100D8:
        vm_net_trace_battle_pool_probe("battle_render_layout_r9_2044_load", tracePc, traceCodeBase);
        break;
    case 0x100E6:
        vm_net_trace_battle_pool_probe("battle_render_layout_cb18_first_load", tracePc, traceCodeBase);
        break;
    case 0x100EC:
        vm_net_trace_battle_pool_probe("battle_render_layout_cb18_first_call", tracePc, traceCodeBase);
        break;
    case 0x100F2:
        vm_net_trace_battle_pool_probe("battle_render_layout_first_return_store", tracePc, traceCodeBase);
        break;
    case 0x100F4:
        vm_net_trace_battle_pool_probe("battle_render_layout_cb18_second_load", tracePc, traceCodeBase);
        break;
    case 0x100FA:
        vm_net_trace_battle_pool_probe("battle_render_layout_cb18_second_call", tracePc, traceCodeBase);
        break;
    case 0x10100:
        vm_net_trace_battle_pool_probe("battle_render_layout_after_second_return", tracePc, traceCodeBase);
        break;
    case 0x10108:
        vm_net_trace_battle_pool_probe("battle_render_layout_scale_calc", tracePc, traceCodeBase);
        break;
    case 0x10110:
        vm_net_trace_battle_pool_probe("battle_render_layout_crash_pc", tracePc, traceCodeBase);
        break;
    case 0x10122:
        vm_net_trace_battle_pool_probe("battle_render_layout_draw_obj_load", tracePc, traceCodeBase);
        break;
    case 0x1012A:
        vm_net_trace_battle_pool_probe("battle_render_layout_draw_cb10_load", tracePc, traceCodeBase);
        break;
    case 0x10136:
        vm_net_trace_battle_pool_probe("battle_render_layout_draw_cb10_call", tracePc, traceCodeBase);
        break;
    case 0x17AC:
        vm_net_trace_battle_pool_probe("battle_event7_dispatch_entry_sub_17AC", tracePc, traceCodeBase);
        break;
    case 0x1820:
        vm_net_trace_battle_pool_probe("battle_event7_kind4_call_sub_7BD0", tracePc, traceCodeBase);
        break;
    case 0x39BE:
        vm_net_trace_battle_pool_probe("battle_event7_common_tail_39BE", tracePc, traceCodeBase);
        break;
    case 0x3604:
        vm_net_trace_battle_pool_probe("battle_screen_callback_3604", tracePc, traceCodeBase);
        break;
    case 0x3D92:
        vm_net_trace_battle_pool_probe("sub_31FA_loop_check_entry_3D92", tracePc, traceCodeBase);
        break;
    case 0x3D9E:
        vm_net_trace_battle_pool_probe("sub_31FA_stack_counter_read_3D9E", tracePc, traceCodeBase);
        break;
    case 0x3DAA:
        vm_net_trace_battle_pool_probe("sub_31FA_loop_compare_3DAA", tracePc, traceCodeBase);
        break;
    case 0x3DBC:
        vm_net_trace_battle_pool_probe("sub_31FA_bridge_cb18_load_3DBC", tracePc, traceCodeBase);
        break;
    case 0x3DC0:
        vm_net_trace_battle_pool_probe("sub_31FA_bridge_cb18_call_3DC0", tracePc, traceCodeBase);
        break;
    case 0x3DC8:
        vm_net_trace_battle_pool_probe("sub_31FA_after_bridge_first_3DC8", tracePc, traceCodeBase);
        break;
    case 0x3DD0:
        vm_net_trace_battle_pool_probe("sub_31FA_bridge_cb18_reload_3DD0", tracePc, traceCodeBase);
        break;
    case 0x440E:
        vm_net_trace_battle_pool_probe("battle_screen_crash_near_440E", tracePc, traceCodeBase);
        break;
    case 0x3DD2:
        vm_net_trace_battle_pool_probe("sub_31FA_bridge_second_r0_zero_3DD2", tracePc, traceCodeBase);
        break;
    case 0x3DD4:
        vm_net_trace_battle_pool_probe("sub_31FA_bridge_second_call_3DD4", tracePc, traceCodeBase);
        break;
    case 0x3DDC:
        vm_net_trace_battle_pool_probe("sub_31FA_after_bridge_second_3DDC", tracePc, traceCodeBase);
        break;
    case 0x3DDE:
        vm_net_trace_battle_pool_probe("sub_31FA_crash_pc_3DDE", tracePc, traceCodeBase);
        break;
    case 0x3DE2:
        vm_net_trace_battle_pool_probe("sub_31FA_stack_ptr_byte_read_3DE2", tracePc, traceCodeBase);
        break;
    case 0x3DE4:
        vm_net_trace_battle_pool_probe("sub_31FA_stack_counter_after_3DE4", tracePc, traceCodeBase);
        break;
    case 0x3E02:
        vm_net_trace_battle_pool_probe("battle_event7_common_loop_entry_3E02", tracePc, traceCodeBase);
        break;
    case 0x3E10:
        vm_net_trace_battle_pool_probe("sub_31FA_draw_call_3E10", tracePc, traceCodeBase);
        break;
    case 0x3E1A:
        vm_net_trace_battle_pool_probe("battle_event7_common_finish_3E1A", tracePc, traceCodeBase);
        break;
    case 0x3E34:
        vm_net_trace_battle_pool_probe("battle_event7_common_post_finish_3E34", tracePc, traceCodeBase);
        break;
    case 0x66A4:
        vm_net_trace_battle_pool_probe("sub_66A4_enemy_lookup_entry", tracePc, traceCodeBase);
        break;
    case 0x66BA:
        vm_net_trace_battle_pool_probe("sub_66A4_enemy_lookup_compare", tracePc, traceCodeBase);
        break;
    case 0x66C8:
        vm_net_trace_battle_pool_probe("sub_66A4_enemy_lookup_miss", tracePc, traceCodeBase);
        break;
    case 0x66CC:
        vm_net_trace_battle_pool_probe("sub_66CC_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_module_state("sub_66CC_entry", tracePc);
        vm_net_trace_battle_local_action_state("sub_66CC_entry", tracePc);
        break;
    case 0x6714:
        vm_net_trace_battle_pool_probe("sub_66CC_stream_init_after_battleinfo", tracePc, traceCodeBase);
        break;
    case 0x6722:
        vm_net_trace_battle_pool_probe("sub_66CC_own_count_store", tracePc, traceCodeBase);
        break;
    case 0x68F8:
        vm_net_trace_battle_pool_probe("sub_66CC_own_visual_lookup", tracePc, traceCodeBase);
        break;
    case 0x6A38:
        vm_net_trace_battle_pool_probe("sub_66CC_enemy_lookup_callsite", tracePc, traceCodeBase);
        break;
    case 0x6B26:
        vm_net_trace_battle_pool_probe("sub_66CC_enemy_lookup_failed_path", tracePc, traceCodeBase);
        break;
    case 0x6BEC:
        vm_net_trace_battle_pool_probe("sub_66CC_before_return", tracePc, traceCodeBase);
        vm_net_trace_battle_module_state("sub_66CC_before_return", tracePc);
        vm_net_trace_battle_local_action_state("sub_66CC_before_return", tracePc);
        break;
    case 0x6D12:
        vm_net_trace_battle_pool_probe("sub_actioninfo_wrapper_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_wrapper_detail("entry", tracePc);
        break;
    case 0x6D20:
        vm_net_trace_battle_pool_probe("sub_actioninfo_wrapper_field_found", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_wrapper_detail("field_found", tracePc);
        break;
    case 0x6D4C:
        vm_net_trace_battle_pool_probe("sub_actioninfo_wrapper_count_ready", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_wrapper_detail("count_ready", tracePc);
        break;
    case 0x6D58:
        vm_net_trace_battle_pool_probe("sub_actioninfo_wrapper_count_read", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_wrapper_detail("count_read", tracePc);
        break;
    case 0x6D6A:
        vm_net_trace_battle_pool_probe("sub_actioninfo_wrapper_record_ptr_len", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_wrapper_detail("record_ptr_len", tracePc);
        break;
    case 0x6DBC:
        vm_net_trace_battle_pool_probe("sub_teaminfo_wrapper_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_teaminfo_wrapper_detail("entry", tracePc);
        break;
    case 0x6DDC:
        vm_net_trace_battle_pool_probe("sub_teaminfo_wrapper_field_found", tracePc, traceCodeBase);
        vm_net_trace_battle_teaminfo_wrapper_detail("field_found", tracePc);
        break;
    case 0x6DF0:
        vm_net_trace_battle_pool_probe("sub_teaminfo_wrapper_count_ready", tracePc, traceCodeBase);
        vm_net_trace_battle_teaminfo_wrapper_detail("count_ready", tracePc);
        break;
    case 0x6E0A:
        vm_net_trace_battle_pool_probe("sub_teaminfo_wrapper_member_id_read", tracePc, traceCodeBase);
        vm_net_trace_battle_teaminfo_wrapper_detail("member_id_read", tracePc);
        break;
    case 0x6E24:
        vm_net_trace_battle_pool_probe("sub_teaminfo_wrapper_link_value_read", tracePc, traceCodeBase);
        vm_net_trace_battle_teaminfo_wrapper_detail("link_value_read", tracePc);
        break;
    case 0x6E46:
        vm_net_trace_battle_pool_probe("sub_teaminfo_wrapper_slot_link_store", tracePc, traceCodeBase);
        vm_net_trace_battle_teaminfo_wrapper_detail("slot_link_store", tracePc);
        break;
    case 0x6E5C:
        vm_net_trace_battle_pool_probe("sub_teaminfo_wrapper_loop_compare", tracePc, traceCodeBase);
        vm_net_trace_battle_teaminfo_wrapper_detail("loop_compare", tracePc);
        break;
    case 0x743C:
        vm_net_trace_battle_pool_probe("sub_743C_status7_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("sub_743C_status7_entry", tracePc);
        break;
    case 0x7516:
        vm_net_trace_battle_pool_probe("sub_743C_result_read", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("sub_743C_result_read", tracePc);
        break;
    case 0x7578:
        vm_net_trace_battle_pool_probe("sub_743C_itemnum_read", tracePc, traceCodeBase);
        break;
    case 0x7592:
        vm_net_trace_battle_pool_probe("sub_743C_iteminfo_read", tracePc, traceCodeBase);
        break;
    case 0x75A2:
        vm_net_trace_battle_pool_probe("sub_743C_item_stream_init", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_item_record_detail("item_stream_init", tracePc);
        break;
    case 0x7614:
        vm_net_trace_battle_pool_probe("sub_743C_item_record_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_item_record_detail("item_record_entry", tracePc);
        break;
    case 0x7638:
        vm_net_trace_battle_pool_probe("sub_743C_item_record_id_match", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_item_record_detail("item_record_id_match", tracePc);
        break;
    case 0x766E:
        vm_net_trace_battle_pool_probe("sub_743C_item_record_blob_ptr", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_item_record_detail("item_record_blob_ptr", tracePc);
        break;
    case 0x7676:
        vm_net_trace_battle_pool_probe("sub_743C_item_record_type_branch", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_item_record_detail("item_record_type_branch", tracePc);
        break;
    case 0x760A:
        vm_net_trace_battle_pool_probe("sub_743C_crash_lastpc_candidate", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_item_record_detail("crash_lastpc_candidate", tracePc);
        break;
    case 0x76DE:
        vm_net_trace_battle_pool_probe("sub_743C_item_record_value_threshold", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_item_record_detail("item_record_value_threshold", tracePc);
        break;
    case 0x771C:
        vm_net_trace_battle_pool_probe("sub_743C_item_record_type3_check", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_item_record_detail("item_record_type3_check", tracePc);
        break;
    case 0x7778:
        vm_net_trace_battle_pool_probe("sub_743C_item_record_nonmatch_branch", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_item_record_detail("item_record_nonmatch_branch", tracePc);
        break;
    case 0x78C6:
        vm_net_trace_battle_pool_probe("sub_743C_item_loop_compare", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_item_record_detail("item_loop_compare", tracePc);
        break;
    case 0x78F0:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_read", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_read", tracePc);
        break;
    case 0x7908:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_stream_init", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_stream_init", tracePc);
        vm_net_trace_battle_local_action_state("sub_743C_combatinfo_stream_init", tracePc);
        break;
    case 0x7910:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_word0_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_word0_store", tracePc);
        break;
    case 0x791E:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_word1_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_word1_store", tracePc);
        break;
    case 0x792E:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_word2_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_word2_store", tracePc);
        break;
    case 0x793E:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_word3_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_word3_store", tracePc);
        break;
    case 0x794E:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_word4_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_word4_store", tracePc);
        break;
    case 0x795E:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_word5_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_word5_store", tracePc);
        break;
    case 0x796E:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_half0_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_half0_store", tracePc);
        break;
    case 0x797E:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_half1_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_half1_store", tracePc);
        break;
    case 0x798E:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_flag_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_flag_store", tracePc);
        break;
    case 0x79A6:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_bonus_gold_read", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_bonus_gold_read", tracePc);
        break;
    case 0x79BA:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_bonus_half0_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_bonus_half0_store", tracePc);
        break;
    case 0x79CE:
        vm_net_trace_battle_pool_probe("sub_743C_combatinfo_bonus_half1_store", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("combatinfo_bonus_half1_store", tracePc);
        break;
    case 0x7A10:
        vm_net_trace_battle_pool_probe("sub_743C_autorevive_read", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("autorevive_read", tracePc);
        vm_net_trace_battle_local_action_state("sub_743C_autorevive_read", tracePc);
        break;
    case 0x7A0C:
        vm_net_trace_battle_pool_probe("sub_743C_before_sub_7228", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_sub7228_detail("before_sub_7228", tracePc);
        break;
    case 0x7A38:
        vm_net_trace_battle_pool_probe("sub_743C_info_ptr_read", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("info_ptr_read", tracePc);
        break;
    case 0x7A42:
        vm_net_trace_battle_pool_probe("sub_743C_info_len_read", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("info_len_read", tracePc);
        break;
    case 0x7A56:
        vm_net_trace_battle_pool_probe("sub_743C_info_apply_call", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("info_apply_call", tracePc);
        break;
    case 0x7A2E:
        vm_net_trace_battle_pool_probe("sub_743C_autorevive_clear_call", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("autorevive_clear_call", tracePc);
        vm_net_trace_battle_local_action_state("sub_743C_autorevive_clear_call", tracePc);
        break;
    case 0x7AA8:
        vm_net_trace_battle_pool_probe("sub_743C_fbs_ptr_read", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("fbs_ptr_read", tracePc);
        break;
    case 0x7AB2:
        vm_net_trace_battle_pool_probe("sub_743C_fbs_len_read", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("fbs_len_read", tracePc);
        break;
    case 0x7B00:
        vm_net_trace_battle_pool_probe("sub_743C_fdata_ptr_read", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("fdata_ptr_read", tracePc);
        break;
    case 0x7B0A:
        vm_net_trace_battle_pool_probe("sub_743C_fdata_len_read", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_combatinfo_detail("fdata_len_read", tracePc);
        break;
    case 0x7BD0:
        vm_net_trace_battle_pool_probe("sub_7BD0_kind4_dispatch_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("dispatch_entry", tracePc);
        break;
    case 0x7BF6:
        vm_net_trace_battle_pool_probe("sub_7BD0_subtype_switch", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("subtype_switch", tracePc);
        break;
    case 0x7C16:
        vm_net_trace_battle_pool_probe("sub_7BD0_case11_result_read", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case11_result_read", tracePc);
        break;
    case 0x7C20:
        vm_net_trace_battle_pool_probe("sub_7BD0_case11_result_after_read", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case11_result_after_read", tracePc);
        break;
    case 0x7C30:
        vm_net_trace_battle_pool_probe("sub_7BD0_case11_type_after_read", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case11_type_after_read", tracePc);
        break;
    case 0x7C9A:
        vm_net_trace_battle_pool_probe("sub_7BD0_case11_failure", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case11_failure", tracePc);
        break;
    case 0x7CB2:
        vm_net_trace_battle_pool_probe("sub_7BD0_case1_9_result_read", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case1_9_result_read", tracePc);
        break;
    case 0x7CBA:
        vm_net_trace_battle_pool_probe("sub_7BD0_case1_9_result_after_read", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case1_9_result_after_read", tracePc);
        break;
    case 0x6EB0:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_parser_detail("entry", tracePc);
        break;
    case 0x6E02:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_actioninfo_ptr_ready", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_parser_detail("actioninfo_ptr_ready", tracePc);
        break;
    case 0x6ED8:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_actioninfo_read", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_parser_detail("actioninfo_read", tracePc);
        break;
    case 0x6EE2:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_actioninfo_result", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_field_result_detail("actioninfo_result", tracePc);
        break;
    case 0x6E1E:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_actionnum_ready", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_parser_detail("actionnum_ready", tracePc);
        break;
    case 0x6EFA:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_actionnum_read", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_parser_detail("actionnum_read", tracePc);
        break;
    case 0x6EFE:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_actionnum_result", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_field_result_detail("actionnum_result", tracePc);
        break;
    case 0x6F0C:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_loop_init", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_loop_detail("loop_init", tracePc);
        break;
    case 0x6E6A:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_type_branch_check", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_parser_detail("type_branch_check", tracePc);
        break;
    case 0x6E74:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_subcount_read", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_parser_detail("subcount_read", tracePc);
        break;
    case 0x6ED4:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_type1_branch", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_parser_detail("type1_branch", tracePc);
        break;
    case 0x6F4E:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_type2_branch", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_parser_detail("type2_branch", tracePc);
        break;
    case 0x6F38:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_header_type_read", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_stream_read_detail("header_type_read", tracePc);
        break;
    case 0x31FA:
        vm_net_trace_battle_pool_probe("draw_battle_anim_effect_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_anim_effect_delta_detail("anim_entry", tracePc);
        break;
    case 0x3448:
        vm_net_trace_battle_pool_probe("draw_battle_anim_effect_after_action_slots", tracePc, traceCodeBase);
        vm_net_trace_battle_anim_effect_delta_detail("anim_after_action_slots", tracePc);
        break;
    case 0x3492:
        vm_net_trace_battle_pool_probe("draw_battle_anim_effect_optional_counter_gate", tracePc, traceCodeBase);
        vm_net_trace_battle_anim_effect_delta_detail("anim_optional_counter_gate", tracePc);
        break;
    case 0x34BC:
        vm_net_trace_battle_pool_probe("draw_battle_anim_effect_before_effect_count", tracePc, traceCodeBase);
        vm_net_trace_battle_anim_effect_delta_detail("anim_before_effect_count", tracePc);
        break;
    case 0x34E4:
        vm_net_trace_battle_pool_probe("draw_battle_anim_effect_effect_count_check", tracePc, traceCodeBase);
        vm_net_trace_battle_anim_effect_delta_detail("anim_effect_count_check", tracePc);
        break;
    case 0x4582:
        vm_net_trace_battle_pool_probe("battle_damage_number_effect_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_anim_effect_delta_detail("damage_number_effect_entry", tracePc);
        break;
    case 0x6F44:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_header_actor_map", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_stream_read_detail("header_actor_map", tracePc);
        break;
    case 0x6F5A:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_header_subcount_read", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_stream_read_detail("header_subcount_read", tracePc);
        break;
    case 0x6F7E:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_child_actor_map", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_stream_read_detail("child_actor_map", tracePc);
        break;
    case 0x6F86:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_child_flag_read", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_stream_read_detail("child_flag_read", tracePc);
        break;
    case 0x6F8E:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_child_valueA_read", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_stream_read_detail("child_valueA_read", tracePc);
        break;
    case 0x6F96:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_child_valueB_read", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_stream_read_detail("child_valueB_read", tracePc);
        break;
    case 0x7006:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_type1_materialize_before_template_copy", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_materialize_detail("type1_before_template_copy", tracePc);
        break;
    case 0x701A:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_type1_materialize_before_callback", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_materialize_detail("type1_before_callback", tracePc);
        vm_net_trace_battle_local_action_state("type1_before_callback", tracePc);
        break;
    case 0x702C:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_type1_after_callback", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_materialize_detail("type1_after_callback", tracePc);
        vm_net_trace_battle_local_action_state("type1_after_callback", tracePc);
        break;
    case 0x7076:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_type2_materialize_before_template_copy", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_materialize_detail("type2_before_template_copy", tracePc);
        break;
    case 0x708A:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_type2_materialize_before_callback", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_materialize_detail("type2_before_callback", tracePc);
        vm_net_trace_battle_local_action_state("type2_before_callback", tracePc);
        break;
    case 0x709C:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_type2_after_callback", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_materialize_detail("type2_after_callback", tracePc);
        vm_net_trace_battle_local_action_state("type2_after_callback", tracePc);
        break;
    case 0x70FA:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_loop_compare", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_loop_detail("loop_compare", tracePc);
        break;
    case 0x7100:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_loop_exit_check", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_loop_detail("loop_exit_check", tracePc);
        break;
    case 0x7104:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_return", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_loop_detail("return", tracePc);
        vm_net_trace_battle_local_action_state("sub_actioninfo_parser_return", tracePc);
        break;
    case 0x4D74:
        vm_net_trace_battle_pool_probe("battle_damage_dispatch_type6_prepare", tracePc, traceCodeBase);
        vm_net_trace_battle_damage_dispatch_detail("type6_prepare", tracePc);
        break;
    case 0x4D78:
        vm_net_trace_battle_pool_probe("battle_damage_dispatch_type6_call", tracePc, traceCodeBase);
        vm_net_trace_battle_damage_dispatch_detail("type6_call", tracePc);
        break;
    case 0x4D54:
        vm_net_trace_battle_pool_probe("battle_damage_dispatch_gate_type6_check", tracePc, traceCodeBase);
        vm_net_trace_battle_damage_dispatch_detail("type6_gate_check", tracePc);
        break;
    case 0x4D58:
        vm_net_trace_battle_pool_probe("battle_damage_dispatch_gate_type6_result", tracePc, traceCodeBase);
        vm_net_trace_battle_damage_dispatch_detail("type6_gate_result", tracePc);
        break;
    case 0x4D88:
        vm_net_trace_battle_pool_probe("battle_damage_dispatch_type4_fallback_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_damage_dispatch_detail("type4_fallback_entry", tracePc);
        break;
    case 0x4DB8:
        vm_net_trace_battle_pool_probe("battle_damage_dispatch_type4_threshold_result", tracePc, traceCodeBase);
        vm_net_trace_battle_damage_dispatch_detail("type4_threshold_result", tracePc);
        break;
    case 0x4DC0:
        vm_net_trace_battle_pool_probe("battle_damage_dispatch_type4_gate_result", tracePc, traceCodeBase);
        vm_net_trace_battle_damage_dispatch_detail("type4_gate_result", tracePc);
        break;
    case 0x4DC6:
        vm_net_trace_battle_pool_probe("battle_damage_dispatch_type4_prepare", tracePc, traceCodeBase);
        vm_net_trace_battle_damage_dispatch_detail("type4_prepare", tracePc);
        break;
    case 0x4DCA:
        vm_net_trace_battle_pool_probe("battle_damage_dispatch_type4_call", tracePc, traceCodeBase);
        vm_net_trace_battle_damage_dispatch_detail("type4_call", tracePc);
        break;
    case 0x4DCC:
        vm_net_trace_battle_pool_probe("battle_damage_dispatch_type4_post_call", tracePc, traceCodeBase);
        vm_net_trace_battle_damage_dispatch_detail("type4_post_call", tracePc);
        break;
    case 0x4B38:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("state_machine_entry", tracePc);
        break;
    case 0x4B4C:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_before_apply", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("state4_before_apply", tracePc);
        vm_net_trace_battle_anim_effect_delta_detail("state4_before_apply", tracePc);
        break;
    case 0x4B50:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_delta_base", tracePc, traceCodeBase);
        vm_net_trace_battle_anim_effect_delta_detail("state4_delta_base", tracePc);
        break;
    case 0x4B6C:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_apply_call", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("state4_apply_call", tracePc);
        vm_net_trace_battle_apply_detail("state4_apply_call", tracePc);
        vm_net_trace_battle_anim_effect_delta_detail("state4_apply_call", tracePc);
        break;
    case 0x4B72:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_after_hp_a_store", tracePc, traceCodeBase);
        vm_net_trace_battle_anim_effect_delta_detail("state4_after_hp_a_store", tracePc);
        break;
    case 0x4B90:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_after_hp_b_store", tracePc, traceCodeBase);
        vm_net_trace_battle_anim_effect_delta_detail("state4_after_hp_b_store", tracePc);
        break;
    case 0x4BAC:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state3_or_4_followup", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("state3_or_4_followup", tracePc);
        break;
    case 0x4BEC:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_store_target", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("state4_store_target", tracePc);
        vm_net_trace_battle_state4_detail("state4_store_target", tracePc);
        break;
    case 0x4B5C:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_gate_state4cc", tracePc, traceCodeBase);
        vm_net_trace_battle_state4_detail("state4_gate_state4cc", tracePc);
        break;
    case 0x4B7E:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_callback8_call", tracePc, traceCodeBase);
        vm_net_trace_battle_state4_detail("state4_callback8_call", tracePc);
        break;
    case 0x4BB0:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_callback8_tick", tracePc, traceCodeBase);
        vm_net_trace_battle_state4_detail("state4_callback8_tick", tracePc);
        break;
    case 0x4BBC:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_gate_state2", tracePc, traceCodeBase);
        vm_net_trace_battle_state4_detail("state4_gate_state2", tracePc);
        break;
    case 0x4BCC:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_flag52a_check", tracePc, traceCodeBase);
        vm_net_trace_battle_state4_detail("state4_flag52a_check", tracePc);
        break;
    case 0x4BD4:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_target24_check", tracePc, traceCodeBase);
        vm_net_trace_battle_state4_detail("state4_target24_check", tracePc);
        break;
    case 0x4BE0:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_target_copy_prepare", tracePc, traceCodeBase);
        vm_net_trace_battle_state4_detail("state4_target_copy_prepare", tracePc);
        break;
    case 0x4BF0:
        vm_net_trace_battle_pool_probe("battle_action_state_machine_state4_target_copy_store", tracePc, traceCodeBase);
        vm_net_trace_battle_state4_detail("state4_target_copy_store", tracePc);
        break;
    case 0x70DA:
        vm_net_trace_battle_pool_probe("sub_actioninfo_parser_fallback_branch", tracePc, traceCodeBase);
        vm_net_trace_battle_actioninfo_parser_detail("fallback_branch", tracePc);
        break;
    case 0x7CD0:
        vm_net_trace_battle_pool_probe("sub_7BD0_4_2_result1_gate", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case1_9_result1_gate", tracePc);
        break;
    case 0x7CE8:
        vm_net_trace_battle_pool_probe("sub_7BD0_4_2_result2_message", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case1_9_result2_message", tracePc);
        break;
    case 0x7CFA:
        vm_net_trace_battle_pool_probe("sub_7BD0_4_2_result4_message", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case1_9_result4_message", tracePc);
        break;
    case 0x7D0C:
        vm_net_trace_battle_pool_probe("sub_7BD0_4_2_result5_message", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case1_9_result5_message", tracePc);
        break;
    case 0x7D1E:
        vm_net_trace_battle_pool_probe("sub_7BD0_4_2_result3_message", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case1_9_result3_message", tracePc);
        break;
    case 0x7D30:
        vm_net_trace_battle_pool_probe("sub_7BD0_4_2_result7_message", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case1_9_result7_message", tracePc);
        break;
    case 0x7D48:
        vm_net_trace_battle_pool_probe("sub_7BD0_4_2_result0_or_6_or_ge8_cleanup", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case1_9_result_cleanup", tracePc);
        break;
    case 0x7D66:
        vm_net_trace_battle_pool_probe("sub_7BD0_case5_10_battle_start_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case5_10_battle_start_entry", tracePc);
        break;
    case 0x7E12:
        vm_net_trace_battle_pool_probe("sub_7BD0_subtype8_info_ptr_read", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case8_info_ptr_read", tracePc);
        vm_net_trace_battle_operate_subtype8_detail("info_ptr_read", tracePc);
        break;
    case 0x7E1A:
        vm_net_trace_battle_pool_probe("sub_7BD0_subtype8_info_ptr_stored", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case8_info_ptr_stored", tracePc);
        vm_net_trace_battle_operate_subtype8_detail("info_ptr_stored", tracePc);
        break;
    case 0x7E1C:
        vm_net_trace_battle_pool_probe("sub_7BD0_subtype8_info_len_read", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case8_info_len_read", tracePc);
        vm_net_trace_battle_operate_subtype8_detail("info_len_read", tracePc);
        break;
    case 0x7E28:
        vm_net_trace_battle_pool_probe("sub_7BD0_subtype8_result_ready", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case8_result_ready", tracePc);
        vm_net_trace_battle_operate_subtype8_detail("result_ready", tracePc);
        break;
    case 0x7E58:
        vm_net_trace_battle_pool_probe("sub_7BD0_subtype8_info_copy_call", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case8_info_copy_call", tracePc);
        vm_net_trace_battle_operate_subtype8_detail("info_copy_call", tracePc);
        break;
    case 0x7E62:
        vm_net_trace_battle_pool_probe("sub_7BD0_subtype8_apply_gate_check", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case8_apply_gate_check", tracePc);
        vm_net_trace_battle_operate_subtype8_detail("apply_gate_check", tracePc);
        break;
    case 0x7E94:
        vm_net_trace_battle_pool_probe("sub_7BD0_subtype8_apply_call", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case8_apply_call", tracePc);
        vm_net_trace_battle_operate_subtype8_detail("apply_call", tracePc);
        break;
    case 0x7F06:
        vm_net_trace_battle_pool_probe("sub_7BD0_case4_mode2_gate", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case4_mode2_gate", tracePc);
        break;
    case 0x7F14:
        vm_net_trace_battle_pool_probe("sub_7BD0_case4_result_after_read", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case4_result_after_read", tracePc);
        break;
    case 0x7F18:
        vm_net_trace_battle_pool_probe("sub_7BD0_case4_success_cleanup", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case4_success_cleanup", tracePc);
        break;
    case 0x7F4C:
        vm_net_trace_battle_pool_probe("sub_7BD0_case4_failure", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case4_failure", tracePc);
        break;
    case 0x7F64:
        vm_net_trace_battle_pool_probe("sub_7BD0_case6_action_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case6_action_entry", tracePc);
        break;
    case 0x7F76:
        vm_net_trace_battle_pool_probe("sub_7BD0_case6_before_action_parser", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case6_before_action_parser", tracePc);
        break;
    case 0x7F84:
        vm_net_trace_battle_pool_probe("sub_7BD0_case6_after_action_phase_set", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case6_after_action_phase_set", tracePc);
        break;
    case 0x2C50:
        vm_net_trace_battle_pool_probe("sub_4B70_battle_apply_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_module_state("sub_4B70_battle_apply_entry", tracePc);
        vm_net_trace_battle_local_action_state("sub_4B70_battle_apply_entry", tracePc);
        vm_net_trace_battle_apply_detail("sub_4B70_battle_apply_entry", tracePc);
        break;
    case 0x2E94:
        vm_net_trace_battle_pool_probe("sub_4B70_send25_prepare", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("sub_4B70_send25_prepare", tracePc);
        vm_net_trace_battle_apply_detail("sub_4B70_send25_prepare", tracePc);
        break;
    case 0x2EA0:
        vm_net_trace_battle_pool_probe("sub_4B70_send25_call", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("sub_4B70_send25_call", tracePc);
        vm_net_trace_battle_apply_detail("sub_4B70_send25_call", tracePc);
        break;
    case 0x23E4:
        vm_net_trace_battle_pool_probe("sub_4304_clear_action_bytes_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("sub_4304_clear_action_bytes_entry", tracePc);
        break;
    case 0x263E:
        vm_net_trace_battle_pool_probe("sub_455e_action_table_compact_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("sub_455e_action_table_compact_entry", tracePc);
        break;
    case 0x269A:
        vm_net_trace_battle_pool_probe("sub_45ba_action_state_tick_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("sub_45ba_action_state_tick_entry", tracePc);
        break;
    case 0x26A8:
        vm_net_trace_battle_pool_probe("sub_45ba_after_action_table_compact", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("sub_45ba_after_action_table_compact", tracePc);
        break;
    case 0x26C6:
        vm_net_trace_battle_pool_probe("sub_45ba_before_clear_action_bytes", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("sub_45ba_before_clear_action_bytes", tracePc);
        break;
    case 0x26E8:
        vm_net_trace_battle_pool_probe("sub_45ba_after_clear_action_bytes", tracePc, traceCodeBase);
        vm_net_trace_battle_local_action_state("sub_45ba_after_clear_action_bytes", tracePc);
        break;
    case 0x7DF0:
        vm_net_trace_battle_pool_probe("sub_7BD0_before_4_10_parser", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("before_4_10_parser", tracePc);
        break;
    case 0x7DF4:
        vm_net_trace_battle_pool_probe("sub_7BD0_after_4_10_parser", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("after_4_10_parser", tracePc);
        vm_net_trace_battle_module_state("sub_7BD0_after_4_10_parser", tracePc);
        break;
    case 0x805C:
        vm_net_trace_battle_pool_probe("sub_7BD0_status7_result_check", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("case7_settle_result_check", tracePc);
        break;
    case 0x806C:
        vm_net_trace_battle_pool_probe("sub_7BD0_before_status7_parser", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("before_status7_parser", tracePc);
        break;
    case 0x8070:
        vm_net_trace_battle_pool_probe("sub_7BD0_after_status7_parser", tracePc, traceCodeBase);
        vm_net_trace_battle_server_cmd_detail("after_status7_parser", tracePc);
        vm_net_trace_battle_module_state("sub_7BD0_after_status7_parser", tracePc);
        break;
    case 0x8996:
        vm_net_trace_battle_pool_probe("sub_8996_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_module_state("sub_8996_entry", tracePc);
        break;
    case 0x89F0:
        vm_net_trace_battle_pool_probe("sub_8996_25_2_type1_send_type100", tracePc, traceCodeBase);
        vm_net_trace_battle_module_state("sub_8996_25_2_type1_send_type100", tracePc);
        break;
    case 0x7228:
        vm_net_trace_battle_pool_probe("sub_7228_entry", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_sub7228_detail("entry", tracePc);
        break;
    case 0x7258:
        vm_net_trace_battle_pool_probe("sub_7228_guard_byte_check", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_sub7228_detail("guard_byte_check", tracePc);
        break;
    case 0x7344:
        vm_net_trace_battle_pool_probe("sub_7228_guard_fail_return", tracePc, traceCodeBase);
        vm_net_trace_battle_status7_sub7228_detail("guard_fail_return", tracePc);
        break;
    default:
        break;
    }
}

static u32 vm_net_mock_build_response_from_rules(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    static const vm_net_mock_rule rules[] = {
        /* 服务器版本握手。可用 bin/net_mocks/version-handshake.bin 覆盖响应体。 */
        {"version-handshake", "version", "net_mocks/version-handshake.bin", NULL, 0},
    };

    for (u32 i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i)
    {
        const vm_net_mock_rule *rule = &rules[i];
        if (!vm_net_mock_request_contains(request, requestLen, rule->contains))
            continue;
        u32 fileLen = vm_net_mock_load_response_file(rule->responseFile, out, outCap);
        if (fileLen)
        {
            vm_net_trace("mock_rule name=%s source=file path=%s len=%u\n", rule->name, rule->responseFile, fileLen);
            return fileLen;
        }
        if (rule->response && rule->responseLen)
        {
            vm_net_trace("mock_rule name=%s source=builtin len=%u\n", rule->name, rule->responseLen);
            return vm_net_mock_copy_response(rule->response, rule->responseLen, out, outCap);
        }
        vm_net_trace("mock_rule name=%s source=fallback\n", rule->name);
        break;
    }
    return 0;
}

static bool vm_net_mock_put_bytes(u8 *out, u32 outCap, u32 *pos, const void *data, u32 len)
{
    if (*pos + len > outCap)
        return false;
    if (len)
        memcpy(out + *pos, data, len);
    *pos += len;
    return true;
}

static bool vm_net_mock_put_u8(u8 *out, u32 outCap, u32 *pos, u8 value)
{
    return vm_net_mock_put_bytes(out, outCap, pos, &value, 1);
}

static bool vm_net_mock_put_be16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    u8 bytes[2] = {(u8)(value >> 8), (u8)value};
    return vm_net_mock_put_bytes(out, outCap, pos, bytes, sizeof(bytes));
}

static bool vm_net_mock_put_be32(u8 *out, u32 outCap, u32 *pos, u32 value)
{
    u8 bytes[4] = {(u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value};
    return vm_net_mock_put_bytes(out, outCap, pos, bytes, sizeof(bytes));
}

static bool vm_net_mock_put_name(u8 *out, u32 outCap, u32 *pos, const char *name)
{
    u32 nameLen = (u32)strlen(name) + 1;
    if (nameLen > 0xff)
        return false;
    return vm_net_mock_put_u8(out, outCap, pos, (u8)nameLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, name, nameLen - 1) &&
           vm_net_mock_put_u8(out, outCap, pos, 0);
}

static bool vm_net_mock_put_int_field(u8 *out, u32 outCap, u32 *pos, const char *name, u32 value)
{
    return vm_net_mock_put_name(out, outCap, pos, name) &&
           vm_net_mock_put_u8(out, outCap, pos, 0x06) &&
           vm_net_mock_put_be16(out, outCap, pos, 4) &&
           vm_net_mock_put_be32(out, outCap, pos, value);
}

static bool vm_net_mock_put_string_field(u8 *out, u32 outCap, u32 *pos, const char *name, const char *value)
{
    u32 valueLen = (u32)strlen(value);
    return vm_net_mock_put_name(out, outCap, pos, name) &&
           vm_net_mock_put_u8(out, outCap, pos, 0x10) &&
           vm_net_mock_put_be16(out, outCap, pos, (u16)valueLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, value, valueLen);
}

static bool vm_net_mock_put_object_entry(u8 *out, u32 outCap, u32 *pos, const char *name, const u8 *value, u16 valueLen)
{
    u32 nameLen = (u32)strlen(name);
    if (nameLen > 0xff)
        return false;
    return vm_net_mock_put_u8(out, outCap, pos, (u8)nameLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, name, nameLen) &&
           vm_net_mock_put_be16(out, outCap, pos, valueLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, value, valueLen);
}

static bool vm_net_mock_put_object_u8(u8 *out, u32 outCap, u32 *pos, const char *name, u8 value)
{
    u8 encoded[] = {0x00, 0x01, value};
    return vm_net_mock_put_object_entry(out, outCap, pos, name, encoded, sizeof(encoded));
}

static bool vm_net_mock_put_object_ascii_digit(u8 *out, u32 outCap, u32 *pos, const char *name, u8 digit)
{
    return vm_net_mock_put_object_u8(out, outCap, pos, name, (u8)('0' + digit));
}

static bool vm_net_mock_put_object_u16(u8 *out, u32 outCap, u32 *pos, const char *name, u16 value)
{
    u8 encoded[] = {0x00, 0x02, (u8)(value >> 8), (u8)value};
    return vm_net_mock_put_object_entry(out, outCap, pos, name, encoded, sizeof(encoded));
}

static bool vm_net_mock_put_object_u32(u8 *out, u32 outCap, u32 *pos, const char *name, u32 value)
{
    u8 encoded[] = {0x00, 0x04, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value};
    return vm_net_mock_put_object_entry(out, outCap, pos, name, encoded, sizeof(encoded));
}

static bool vm_net_mock_put_object_blob(u8 *out, u32 outCap, u32 *pos, const char *name, const u8 *data, u16 dataLen)
{
    u32 nameLen = (u32)strlen(name);
    u16 valueLen = dataLen + 2;
    if (nameLen > 0xff)
        return false;
    return vm_net_mock_put_u8(out, outCap, pos, (u8)nameLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, name, nameLen) &&
           vm_net_mock_put_be16(out, outCap, pos, valueLen) &&
           vm_net_mock_put_be16(out, outCap, pos, dataLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, data, dataLen);
}

static bool vm_net_mock_put_object_raw(u8 *out, u32 outCap, u32 *pos, const char *name, const u8 *data, u16 dataLen)
{
    return vm_net_mock_put_object_entry(out, outCap, pos, name, data, dataLen);
}

static bool vm_net_mock_put_object_string(u8 *out, u32 outCap, u32 *pos, const char *name, const char *value)
{
    return vm_net_mock_put_object_blob(out, outCap, pos, name, (const u8 *)value, (u16)strlen(value));
}

static bool vm_net_mock_begin_wt_object(u8 *out, u32 outCap, u32 *pos, u8 major, u8 kind, u8 subtype, u32 *objectStart)
{
    if (*pos + 6 > outCap)
        return false;
    if (objectStart)
        *objectStart = *pos;
    out[(*pos)++] = major;
    out[(*pos)++] = kind;
    out[(*pos)++] = subtype;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    return true;
}

static void vm_net_mock_finish_wt_object(u8 *out, u32 objectStart, u32 pos)
{
    u32 objectLen = pos - objectStart;
    out[objectStart + 4] = (u8)(objectLen >> 8);
    out[objectStart + 5] = (u8)objectLen;
}

static void vm_net_mock_finish_wt_packet(u8 *out, u32 pos, u8 objectCount)
{
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = objectCount;
}

static u8 vm_net_mock_env_u8(const char *name, u8 fallback)
{
    const char *spec = getenv(name);
    if (spec == NULL || spec[0] == 0)
        return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(spec, &end, 0);
    if (end == spec)
        return fallback;
    return (u8)parsed;
}

static u32 vm_net_mock_env_u32(const char *name, u32 fallback)
{
    const char *spec = getenv(name);
    if (spec == NULL || spec[0] == 0)
        return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(spec, &end, 0);
    if (end == spec)
        return fallback;
    return (u32)parsed;
}

static const char *vm_net_mock_env_str(const char *name, const char *fallback)
{
    const char *spec = getenv(name);
    if (spec == NULL || spec[0] == 0)
        return fallback;
    return spec;
}

static u32 vm_net_mock_build_version_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 result = 1;
    if (outCap < pos)
        return 0;

    if (g_netMockUpdateDelivered || vm_net_mock_has_installed_update())
    {
        g_netMockUpdateDelivered = 1;
        result = 0;
    }

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x12, 5, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    u8 objectCount = 1;
    if (result == 0)
    {
        /* CBE parses subtype 5 in handle_version_update_response and subtype 9
         * in startup_update_net_callback.  Keep both objects: removing subtype 9
         * leaves the startup screen stuck after update_state becomes 2.
         */
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x12, 9, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "id", 0))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "code", 0))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount = 2;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_version_response result=%u objects=%u delivered=%u\n", result, objectCount, g_netMockUpdateDelivered);
    return pos;
}

static u32 vm_net_mock_build_update_chunk_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 11;
    if (outCap < pos)
        return 0;

    static u8 updateData[131072];
    char requestName[256];
    requestName[0] = 0;
    bool haveRequestName = vm_net_mock_get_object_string_field(request, requestLen, "name", requestName, sizeof(requestName)) &&
                           requestName[0] != 0;
    u8 requestType = 0;
    bool haveRequestType = vm_net_mock_get_object_u8_field(request, requestLen, "type", &requestType);
    const char *payloadName = NULL;
    u32 updateLen = vm_net_mock_load_resource_update_payload(request,
                                                             requestLen,
                                                             haveRequestName ? requestName : NULL,
                                                             updateData,
                                                             sizeof(updateData),
                                                             &payloadName);
    if (updateLen == 0)
        return 0;

    u32 start = 0;
    vm_net_mock_get_object_u32_field(request, requestLen, "start", &start);
    if (start >= updateLen)
        start = 0;

    u32 chunkLen = updateLen - start;
    if (chunkLen > 0x1000)
        chunkLen = 0x1000;
    u32 crc = vm_net_mock_signed_byte_sum(updateData, start + chunkLen);

    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "totalsize", updateLen))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "crc", crc))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "version", (const u8 *)"\x00\x01", 2))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", haveRequestType ? requestType : 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", payloadName ? payloadName : "MMORPGTempcbm"))
        return 0;
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "data", updateData + start, (u16)chunkLen))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 1);
    out[5] = 1;
    out[6] = 0x12;
    out[7] = 7;
    out[8] = 0;
    vm_net_mock_finish_wt_object(out, 5, pos);
    vm_net_trace("mock_update_chunk_response reqName=%s reqType=%u name=%s type=%u start=%u chunk=%u totalsize=%u crc=%u\n",
                 haveRequestName ? requestName : "<empty>",
                 haveRequestType ? requestType : 0,
                 payloadName ? payloadName : "MMORPGTempcbm",
                 haveRequestType ? requestType : 0,
                 start,
                 chunkLen,
                 updateLen,
                 crc);
    return pos;
}

static bool vm_net_mock_seq_put_u32(u8 *out, u32 outCap, u32 *pos, u32 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 4) &&
           vm_net_mock_put_be32(out, outCap, pos, value);
}

static bool vm_net_mock_seq_put_u24(u8 *out, u32 outCap, u32 *pos, u32 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 4) &&
           vm_net_mock_put_u8(out, outCap, pos, (u8)(value >> 16)) &&
           vm_net_mock_put_u8(out, outCap, pos, (u8)(value >> 8)) &&
           vm_net_mock_put_u8(out, outCap, pos, (u8)value);
}

static bool vm_net_mock_seq_put_u8(u8 *out, u32 outCap, u32 *pos, u8 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 1) &&
           vm_net_mock_put_u8(out, outCap, pos, value);
}

static bool vm_net_mock_seq_put_i16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 2) &&
           vm_net_mock_put_be16(out, outCap, pos, value);
}

static bool vm_net_mock_seq_put_string(u8 *out, u32 outCap, u32 *pos, const char *value)
{
    u16 len = value ? (u16)(strlen(value) + 1) : 1;
    return vm_net_mock_put_be16(out, outCap, pos, len) &&
           vm_net_mock_put_bytes(out, outCap, pos, value ? value : "", len);
}

static bool vm_net_mock_seq_put_string_list(
    u8 *out, u32 outCap, u32 *pos, const char *const *values, u32 count)
{
    u32 i = 0;
    for (i = 0; i < count; ++i)
    {
        if (!vm_net_mock_seq_put_string(out, outCap, pos, values[i]))
            return false;
    }
    return true;
}

static u32 vm_net_mock_build_pos_info(u8 *out, u32 outCap, u16 x, u16 y)
{
    u32 pos = 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, x))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, y))
        return 0;
    return pos;
}

typedef struct
{
    char magic[4];
    char scene[64];
    u16 x;
    u16 y;
} vm_net_mock_player_pos_state;

static const char *vm_net_mock_default_scene_name(void);

static vm_net_mock_player_pos_state g_vm_net_mock_player_pos;
static bool g_vm_net_mock_player_pos_loaded = false;
static bool g_vm_net_mock_player_pos_valid = false;

static bool vm_net_mock_snapshot_current_player_pos(const char *reason);

static bool vm_net_mock_str_ends_with(const char *text, const char *suffix)
{
    size_t textLen = text ? strlen(text) : 0;
    size_t suffixLen = suffix ? strlen(suffix) : 0;
    if (suffixLen == 0 || textLen < suffixLen)
        return false;
    return strcmp(text + textLen - suffixLen, suffix) == 0;
}

static bool vm_net_mock_scene_name_has_path_separator(const char *scene)
{
    if (scene == NULL)
        return true;
    for (const char *p = scene; *p; ++p)
    {
        if (*p == '/' || *p == '\\' || *p == ':' || (u8)*p < 0x20)
            return true;
    }
    return false;
}

static bool vm_net_mock_scene_resource_exists(const char *scene)
{
    char path[256];
    FILE *fp = NULL;
    if (scene == NULL || scene[0] == 0 || vm_net_mock_scene_name_has_path_separator(scene))
        return false;

    snprintf(path, sizeof(path), "JHOnlineData/%s", scene);
    fp = fopen(path, "rb");
    if (fp)
    {
        fclose(fp);
        return true;
    }

    if (!vm_net_mock_str_ends_with(scene, ".sce"))
    {
        snprintf(path, sizeof(path), "JHOnlineData/%s.sce", scene);
        fp = fopen(path, "rb");
        if (fp)
        {
            fclose(fp);
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_scene_name_is_safe(const char *scene)
{
    if (scene == NULL || scene[0] == 0)
        return false;
    return vm_net_mock_scene_resource_exists(scene);
}

static const char *vm_net_mock_normalize_scene_name_for_enter(const char *scene)
{
    static char normalized[64];
    if (!vm_net_mock_scene_name_is_safe(scene))
        return vm_net_mock_default_scene_name();

    /*
     * Fresh actorinfo/sceneKey historically used extensionless c-prefixed town
     * keys (`c00..._01`), which the local file layer resolves to `.sce`.
     * Replaying `c00..._NN.sce` directly can be mistaken for a downloadable
     * resource key on re-enter, so strip only that c-prefixed suffix form.
     */
    if (scene[0] == 'c' && vm_net_mock_str_ends_with(scene, ".sce"))
    {
        size_t len = strlen(scene) - 4;
        if (len >= sizeof(normalized))
            len = sizeof(normalized) - 1;
        memcpy(normalized, scene, len);
        normalized[len] = 0;
        return normalized;
    }
    return scene;
}

static void vm_net_mock_player_pos_path(char *path, size_t pathSize)
{
    snprintf(path, pathSize, "nvram/jhol_mock_player_pos.bin");
}

static void vm_net_mock_load_player_pos_state(void)
{
    char path[128];
    vm_net_mock_player_pos_state state;

    if (g_vm_net_mock_player_pos_loaded)
        return;
    g_vm_net_mock_player_pos_loaded = true;
    memset(&g_vm_net_mock_player_pos, 0, sizeof(g_vm_net_mock_player_pos));

    vm_net_mock_player_pos_path(path, sizeof(path));
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return;
    size_t readLen = fread(&state, 1, sizeof(state), fp);
    fclose(fp);

    if (readLen == sizeof(state) &&
        memcmp(state.magic, "JHP1", 4) == 0 &&
        state.x > 0 &&
        state.y > 0)
    {
        g_vm_net_mock_player_pos = state;
        g_vm_net_mock_player_pos.scene[sizeof(g_vm_net_mock_player_pos.scene) - 1] = 0;
        if (!vm_net_mock_scene_name_is_safe(g_vm_net_mock_player_pos.scene))
        {
            vm_net_trace("mock_player_pos_load_reject_scene raw=%s path=%s\n",
                         g_vm_net_mock_player_pos.scene,
                         path);
            snprintf(g_vm_net_mock_player_pos.scene, sizeof(g_vm_net_mock_player_pos.scene),
                     "%s", vm_net_mock_default_scene_name());
        }
        g_vm_net_mock_player_pos_valid = true;
        vm_net_trace("mock_player_pos_load scene=%s pos=%u,%u path=%s\n",
                     g_vm_net_mock_player_pos.scene,
                     (unsigned int)g_vm_net_mock_player_pos.x,
                     (unsigned int)g_vm_net_mock_player_pos.y,
                     path);
    }
}

static void vm_net_mock_save_player_pos_state(const char *scene, u16 x, u16 y, const char *reason)
{
    char path[128];
    if (x == 0 || y == 0)
        return;

    vm_net_mock_load_player_pos_state();
    if (!vm_net_mock_scene_name_is_safe(scene))
    {
        if (g_vm_net_mock_player_pos_valid && vm_net_mock_scene_name_is_safe(g_vm_net_mock_player_pos.scene))
            scene = g_vm_net_mock_player_pos.scene;
        else
            scene = vm_net_mock_default_scene_name();
    }
    memcpy(g_vm_net_mock_player_pos.magic, "JHP1", 4);
    snprintf(g_vm_net_mock_player_pos.scene, sizeof(g_vm_net_mock_player_pos.scene), "%s", scene);
    g_vm_net_mock_player_pos.x = x;
    g_vm_net_mock_player_pos.y = y;
    g_vm_net_mock_player_pos_valid = true;

#ifdef _WIN32
    _mkdir("nvram");
#else
    mkdir("nvram", 0777);
#endif
    vm_net_mock_player_pos_path(path, sizeof(path));
    FILE *fp = fopen(path, "wb");
    if (fp)
    {
        fwrite(&g_vm_net_mock_player_pos, 1, sizeof(g_vm_net_mock_player_pos), fp);
        fclose(fp);
    }
    vm_net_trace("mock_player_pos_save reason=%s scene=%s pos=%u,%u path=%s\n",
                 reason ? reason : "?",
                 g_vm_net_mock_player_pos.scene,
                 (unsigned int)x,
                 (unsigned int)y,
                 path);
}

static void vm_net_mock_mark_pending_scene_pos_save(const char *scene, u16 x, u16 y, const char *reason)
{
    if (!vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0)
        return;
    snprintf(g_vm_net_mock_pending_scene_save_scene, sizeof(g_vm_net_mock_pending_scene_save_scene),
             "%s", scene);
    snprintf(g_vm_net_mock_pending_scene_save_reason, sizeof(g_vm_net_mock_pending_scene_save_reason),
             "%s", reason ? reason : "pending-scene-load");
    g_vm_net_mock_pending_scene_save_x = x;
    g_vm_net_mock_pending_scene_save_y = y;
    g_vm_net_mock_pending_scene_save_valid = true;
    vm_net_trace("mock_player_pos_pending_scene_save scene=%s pos=%u,%u reason=%s\n",
                 g_vm_net_mock_pending_scene_save_scene,
                 (unsigned int)x,
                 (unsigned int)y,
                 g_vm_net_mock_pending_scene_save_reason);
}

static const char *vm_net_mock_current_scene_name(void)
{
    const char *overrideName = vm_net_mock_env_str("CBE_SCENE_KEY", "");
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    vm_net_mock_load_player_pos_state();
    if (g_vm_net_mock_player_pos_valid && vm_net_mock_scene_name_is_safe(g_vm_net_mock_player_pos.scene))
        return vm_net_mock_normalize_scene_name_for_enter(g_vm_net_mock_player_pos.scene);
    return vm_net_mock_default_scene_name();
}

static u16 vm_net_mock_scene_spawn_x(void)
{
    if (getenv("CBE_SCENE_POS_X") != NULL)
        return (u16)vm_net_mock_env_u32("CBE_SCENE_POS_X", 223);
    vm_net_mock_load_player_pos_state();
    if (g_vm_net_mock_player_pos_valid)
        return g_vm_net_mock_player_pos.x;
    return 223;
}

static u16 vm_net_mock_scene_spawn_y(void)
{
    if (getenv("CBE_SCENE_POS_Y") != NULL)
        return (u16)vm_net_mock_env_u32("CBE_SCENE_POS_Y", 382);
    vm_net_mock_load_player_pos_state();
    if (g_vm_net_mock_player_pos_valid)
        return g_vm_net_mock_player_pos.y;
    return 382;
}

static const char *vm_net_mock_default_scene_name(void)
{
    return "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31"; /* GBK: c00PengLaiXianDao_01 */
}

static const char *vm_net_mock_scene_key_name(void)
{
    /*
     * This key is copied by parse_actorinfo_response() into R9+0x5E46 and later
     * reused as the mode-10 descriptor name by scene_rebuild_runtime_nodes().
     * The ASCII experiment proved the update path, but also bypassed the local
     * .sce scene descriptor and left the map background black when cached.
     * Keep the default aligned with 30/1.scene and use CBE_SCENE_KEY for
     * non-colliding descriptor experiments.
     */
    return vm_net_mock_current_scene_name();
}

static const char *vm_net_mock_default_scene_title(void)
{
    return "\xc5\xee\xc0\xb3\x2d\xcd\xad\xc8\xb8\xcc\xa8"; /* GBK: PengLai-TongQueTai */
}

static const char *vm_net_mock_fb_target_info_text(void)
{
    const char *overrideInfo = vm_net_mock_env_str("CBE_FB_TARGET_INFO", "");
    if (overrideInfo != NULL && overrideInfo[0] != 0)
        return overrideInfo;
    return vm_net_mock_default_scene_title();
}

static bool vm_net_mock_put_scene_fields_with(u8 *out, u32 outCap, u32 *pos,
                                               bool includeResult, bool includeType, u8 requestType,
                                               const char *sceneName, u16 spawnX, u16 spawnY)
{
    u8 posInfo[8];
    u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), spawnX, spawnY);
    if (posInfoLen == 0)
        return false;
    if (includeResult && !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (includeType && !vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "scene", sceneName ? sceneName : vm_net_mock_default_scene_name()))
        return false;
    return vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen);
}

static bool vm_net_mock_put_scene_ack_without_posinfo(u8 *out, u32 outCap, u32 *pos,
                                                      u8 requestType, const char *sceneName)
{
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    return vm_net_mock_put_object_string(out, outCap, pos, "scene",
                                         sceneName ? sceneName : vm_net_mock_default_scene_name());
}

static bool vm_net_mock_put_scene_fields(u8 *out, u32 outCap, u32 *pos, bool includeResult, bool includeType, u8 requestType)
{
    return vm_net_mock_put_scene_fields_with(out, outCap, pos,
                                             includeResult, includeType, requestType,
                                             vm_net_mock_current_scene_name(),
                                             vm_net_mock_scene_spawn_x(),
                                             vm_net_mock_scene_spawn_y());
}

typedef struct
{
    char scene[64];
    u16 x;
    u16 y;
    u32 exitId;
} vm_net_mock_scene_change_target;

static vm_net_mock_scene_change_target g_vm_net_mock_last_scene_change_target;
static bool g_vm_net_mock_last_scene_change_target_valid = false;
static bool g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
static u8 g_vm_net_mock_last_scene_change_fb4_type = 1;
static vm_net_mock_scene_change_target g_vm_net_mock_last_completed_scene_change_target;
static bool g_vm_net_mock_last_completed_scene_change_target_valid = false;
static u32 g_vm_net_mock_last_completed_scene_change_tick = 0;

static void vm_net_mock_remember_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (target == NULL || target->scene[0] == 0)
        return;
    g_vm_net_mock_last_scene_change_target = *target;
    g_vm_net_mock_last_scene_change_target_valid = true;
}

static bool vm_net_mock_scene_change_targets_equal(const vm_net_mock_scene_change_target *a,
                                                   const vm_net_mock_scene_change_target *b)
{
    return a != NULL && b != NULL &&
           a->x == b->x &&
           a->y == b->y &&
           a->exitId == b->exitId &&
           strcmp(a->scene, b->scene) == 0;
}

static void vm_net_mock_mark_completed_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (target == NULL || target->scene[0] == 0)
        return;
    g_vm_net_mock_last_completed_scene_change_target = *target;
    g_vm_net_mock_last_completed_scene_change_target_valid = true;
    g_vm_net_mock_last_completed_scene_change_tick = g_schedulerTick;
}

static bool vm_net_mock_is_recent_completed_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (!g_vm_net_mock_last_completed_scene_change_target_valid ||
        !vm_net_mock_scene_change_targets_equal(target, &g_vm_net_mock_last_completed_scene_change_target))
    {
        return false;
    }
    return (g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick) < 90;
}

static void vm_net_mock_get_scene_change_target(const u8 *request, u32 requestLen,
                                                vm_net_mock_scene_change_target *target)
{
    char mapId[64];
    u32 exitId = 0;
    memset(target, 0, sizeof(*target));
    snprintf(target->scene, sizeof(target->scene), "%s", vm_net_mock_default_scene_name());
    target->x = vm_net_mock_scene_spawn_x();
    target->y = vm_net_mock_scene_spawn_y();
    target->exitId = 0;

    if (!vm_net_mock_get_object_string_field(request, requestLen, "mapID", mapId, sizeof(mapId)))
        return;
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "exitID", &exitId);
    target->exitId = exitId;

    if (mapId[0] != 0)
        snprintf(target->scene, sizeof(target->scene), "%s", mapId);

    if (strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65") == 0)
    {
        if (exitId == 1)
        {
            target->x = 128;
            target->y = 45;
        }
        else
        {
            target->x = 396;
            target->y = 473;
        }
    }
    else if (strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0 ||
             strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31") == 0)
    {
        target->x = 223;
        target->y = 382;
    }
    else if (strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0 ||
             strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0)
    {
        target->x = 105;
        target->y = (exitId == 1) ? 58 : 395;
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 230 : 225;
        target->y = (exitId == 1) ? 425 : 116;
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1 || exitId == 3) ? 305 : 80;
        target->y = (exitId == 1 || exitId == 3) ? 310 : 60;
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1 || exitId == 2) ? 40 : 200;
        target->y = (exitId == 1 || exitId == 2) ? 70 : 540;
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1 || exitId == 2) ? 323 : 42;
        target->y = (exitId == 1 || exitId == 2) ? 200 : 60;
    }
    else if (strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1 || exitId == 3) ? 256 : 136;
        target->y = (exitId == 1 || exitId == 3) ? 300 : 58;
    }
}

static bool vm_net_mock_append_group_info_object(u8 *out, u32 outCap, u32 *pos, u32 leadId)
{
    u8 groupInfo[128];
    u32 groupInfoLen = 0;
    u32 templateId = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_ID", 105);
    u32 templateHp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_HP", 20);
    u32 templateMaxHp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_MAX_HP", templateHp);
    u32 templateMp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_MP", 20);
    u32 templateMaxMp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_MAX_MP", templateMp);
    u8 templateByte0 = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_BYTE0", 1);
    u8 templateByte1 = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_BYTE1", 0);
    u8 templateByte2 = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_BYTE2", 0);
    const char *templateName = vm_net_mock_env_str("CBE_GROUPINFO_TEMPLATE_NAME", "Monster");
    bool seedTemplate = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_SEED", 0) != 0 && templateId != 0;
    u8 num = 0;
    u32 objectStart = 0;

    if (seedTemplate)
    {
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateId))
            return false;
        if (!vm_net_mock_seq_put_string(groupInfo, sizeof(groupInfo), &groupInfoLen, templateName))
            return false;
        if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, templateByte0))
            return false;
        if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, templateByte1))
            return false;
        if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, templateByte2))
            return false;
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateHp))
            return false;
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxHp))
            return false;
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMp))
            return false;
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxMp))
            return false;
        num = 1;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "num", num))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", groupInfo, groupInfoLen))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "leadid", leadId))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    vm_net_trace("mock_group_info_template_seed enabled=%u leadId=%u num=%u templateId=%u name=%s bytes=%u,%u,%u stats=%u,%u,%u,%u groupinfoLen=%u"
                 " evidence=IDA:net_handle_group_info_0x01011F3A_reads_num_groupinfo_leadid_and_calls_AddRoleToList_0x01011E1E\n",
                 seedTemplate ? 1 : 0,
                 leadId,
                 num,
                 templateId,
                 templateName ? templateName : "",
                 templateByte0,
                 templateByte1,
                 templateByte2,
                 templateHp,
                 templateMaxHp,
                 templateMp,
                 templateMaxMp,
                 groupInfoLen);
    return true;
}

static bool vm_net_mock_append_battle_enemy_template_prefill_object(u8 *out, u32 outCap,
                                                                    u32 *pos, u32 templateId)
{
    u8 groupInfo[128];
    u32 groupInfoLen = 0;
    u32 templateHp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_HP", 20);
    u32 templateMaxHp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_MAX_HP", templateHp);
    u32 templateMp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_MP", 20);
    u32 templateMaxMp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_MAX_MP", templateMp);
    u8 rowByte34 = vm_net_mock_env_u8("CBE_BATTLE_PREFILL_TEMPLATE_BYTE34", 1);
    u8 rowByte35 = vm_net_mock_env_u8("CBE_BATTLE_PREFILL_TEMPLATE_BYTE35", 0);
    const char *templateName = vm_net_mock_env_str("CBE_BATTLE_PREFILL_TEMPLATE_NAME", "Monster");
    u32 objectStart = 0;

    if (templateId == 0)
        return false;

    if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateId))
        return false;
    if (!vm_net_mock_seq_put_string(groupInfo, sizeof(groupInfo), &groupInfoLen, templateName))
        return false;
    if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, rowByte34))
        return false;
    if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, rowByte35))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateHp))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxHp))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMp))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxMp))
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 5, &objectStart))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", groupInfo, groupInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    vm_net_trace("mock_battle_enemy_template_prefill response=5/5 templateId=%u name=%s grammar=raw_id_seq_string_seq_u8_seq_u8_seq_u32x4 bytes=%u,%u stats=%u,%u,%u,%u groupinfoLen=%u evidence=runtime:trace_groupinfo_case5_reader_0101209e_cursor0_blob_starts_innerLen_then_id,runtime:trace_groupinfo_case5_reader_after_id_cursor6_showed_removed_pre_name_byte_was_making_name_len_zero,IDA:net_handle_group_info_subtype5_case5_0x0101208C_reads_groupinfo_via_slots_20_2c_1c_28_28_20x4_then_calls_AddRoleToList_0x01012116\n",
                 templateId,
                 templateName ? templateName : "",
                 rowByte34,
                 rowByte35,
                 templateHp,
                 templateMaxHp,
                 templateMp,
                 templateMaxMp,
                 groupInfoLen);
    return true;
}

static bool vm_net_mock_append_type1_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0a, 0x1a, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", 0))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "name", "Codex"))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "money", 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_misc_player_sync8_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    static const u8 seqOne[] = {0x00, 0x02, 0x00, 0x01};
    /*
     * Focused protocol experiment for the bundled post-login 1/7/7 type=1
     * request. Static analysis shows top-level 7 -> local subtype 8 with
     * type=4/result=1 is one of the few branches that can seed the empty
     * type=15 HUD source head and immediately trigger rebuild(2)->rebuild(1).
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 8, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 4))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "seq", seqOne, sizeof(seqOne)))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_misc_player_type_object(u8 *out, u32 outCap, u32 *pos, u8 subtype)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (subtype == 20)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "pcimg", 0))
            return false;
    }
    else if (subtype == 32)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard", 0))
            return false;
    }
    else
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_enter_object_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneName, u16 spawnX, u16 spawnY)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, pos, false, false, 0,
                                           sceneName, spawnX, spawnY))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_pos_result_object_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                                 const char *sceneName, u16 spawnX, u16 spawnY)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 2, &objectStart))
        return false;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, pos, true, true, 2,
                                           sceneName, spawnX, spawnY))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_enter_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_scene_enter_object_for_scene(out, outCap, pos,
                                                          vm_net_mock_current_scene_name(),
                                                          vm_net_mock_scene_spawn_x(),
                                                          vm_net_mock_scene_spawn_y());
}

static bool vm_net_mock_scene_is_penglai02(const char *scene);

static bool vm_net_mock_append_scene_room_npc_object(u8 *out, u32 outCap, u32 *pos)
{
    typedef struct
    {
        u32 actorId;
        u32 x;
        u32 y;
        u32 sceneType;
        const char *actorResource;
        const char *displayName;
        const char *scriptName;
    } vm_net_mock_scene_npcinfo_seed;
    u32 objectStart = 0;
    u8 roomList[128];
    u8 npcInfo[256];
    u8 colNames[64];
    u32 roomListLen = 0;
    u32 npcInfoLen = 0;
    u32 colNamesLen = 0;
    u8 npcNum = 0;
    static const char *const roomColumns[] = {"id", "room", "scene", "state"};
    const char *scene = vm_net_mock_current_scene_name();
    bool seedSceneNpcInfo = vm_net_mock_scene_is_penglai02(scene) &&
                            vm_net_mock_env_u8("CBE_GROUP_TYPE1_SCENE_NPCINFO", 1) != 0;
    if (!vm_net_mock_seq_put_string_list(colNames, sizeof(colNames), &colNamesLen, roomColumns, 4))
        return false;
    if (!vm_net_mock_seq_put_u32(roomList, sizeof(roomList), &roomListLen, 1001))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "1001"))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "Penglai"))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "Scene"))
        return false;
    if (seedSceneNpcInfo)
    {
        static const vm_net_mock_scene_npcinfo_seed penglai02NpcInfoSeeds[] = {
            {20020, 70, 38, 8, "n_swordmaster.actor", "\xc5\xb7\xd2\xb1\xd7\xd3", ""},
            {20021, 108, 38, 8, "e_monkey.actor", "\xd0\xa1\xba\xef\xd7\xd3", ""},
        };
        size_t i;
        for (i = 0; i < sizeof(penglai02NpcInfoSeeds) / sizeof(penglai02NpcInfoSeeds[0]); ++i)
        {
            const vm_net_mock_scene_npcinfo_seed *seed = &penglai02NpcInfoSeeds[i];
            /*
             * scene_parse_npcinfo_and_spawn_npcs() consumes, per row:
             *   u32 id, u32 x, u32 y, str display, str actor, str script,
             *   str optional_lookup_name, u32 sceneType.
             */
            if (!vm_net_mock_seq_put_u32(npcInfo, sizeof(npcInfo), &npcInfoLen, seed->actorId))
                return false;
            if (!vm_net_mock_seq_put_u32(npcInfo, sizeof(npcInfo), &npcInfoLen, seed->x))
                return false;
            if (!vm_net_mock_seq_put_u32(npcInfo, sizeof(npcInfo), &npcInfoLen, seed->y))
                return false;
            if (!vm_net_mock_seq_put_string(npcInfo, sizeof(npcInfo), &npcInfoLen, seed->displayName))
                return false;
            if (!vm_net_mock_seq_put_string(npcInfo, sizeof(npcInfo), &npcInfoLen, seed->actorResource))
                return false;
            if (!vm_net_mock_seq_put_string(npcInfo, sizeof(npcInfo), &npcInfoLen, seed->scriptName))
                return false;
            if (!vm_net_mock_seq_put_string(npcInfo, sizeof(npcInfo), &npcInfoLen, ""))
                return false;
            if (!vm_net_mock_seq_put_u32(npcInfo, sizeof(npcInfo), &npcInfoLen, seed->sceneType))
                return false;
            npcNum += 1;
        }
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 3, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "curpage", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "pagenum", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "colnum", 4))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "colnames", colNames, (u16)colNamesLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "roomnum", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "roomlist", roomList, (u16)roomListLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", npcNum))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "npcinfo", npcInfo, (u16)npcInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    vm_net_trace("mock_scene_room_npc_object scene=%s roomnum=1 npcnum=%u npcinfoLen=%u seedSceneNpcInfo=%u evidence=main:scene_parse_npcinfo_and_spawn_npcs_01037998_row_u32_u32_u32_str_str_str_str_u32\n",
                 scene ? scene : "?",
                 (unsigned int)npcNum,
                 (unsigned int)npcInfoLen,
                 seedSceneNpcInfo ? 1u : 0u);
    return true;
}

static bool vm_net_mock_append_scene_room_roles_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 rolesInfo[128];
    u8 colNames[64];
    u32 rolesInfoLen = 0;
    u32 colNamesLen = 0;
    static const char *const roleColumns[] = {"name", "job", "level", "state"};
    if (!vm_net_mock_seq_put_string_list(colNames, sizeof(colNames), &colNamesLen, roleColumns, 4))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, "Codex"))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, "Warrior"))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, "Lv1"))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, "Ready"))
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 7, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "roomid", 1001))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "colnum", 4))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "colnames", colNames, (u16)colNamesLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "rolenum", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "rolesinfo", rolesInfo, (u16)rolesInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_resource_followup_objects(u8 *out, u32 outCap, u32 *pos,
                                                               u8 *objectCount, bool includeSkillBooks,
                                                               bool includeTaskLists,
                                                               bool includeActorOther, bool includeInfoBanner,
                                                               bool includeFbTargetClear,
                                                               bool includeFbTargetSeedOnly);

static u32 vm_net_mock_build_group_type1_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    if (request == NULL || requestLen < 9 || outCap < 5)
        return 0;

    u32 leadId = 10001;
    bool hasGroup10 = false;
    bool hasType1 = false;
    bool enableMiscSync8 = vm_net_mock_env_u8("CBE_GROUP_TYPE1_MISC_SYNC8", 0) != 0;
    bool includeRoomNpc = vm_net_mock_env_u8("CBE_GROUP_TYPE1_ROOM_NPC", 0) != 0 ||
                          (vm_net_mock_scene_is_penglai02(vm_net_mock_current_scene_name()) &&
                           vm_net_mock_env_u8("CBE_GROUP_TYPE1_SCENE_NPCINFO", 1) != 0);
    bool includeRoomRoles = vm_net_mock_env_u8("CBE_GROUP_TYPE1_ROOM_ROLES", 0) != 0;
    bool includeFbTargetClear = false;
    u32 offset = 4;
    vm_net_mock_request_object object;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.kind == 5 && object.subtype == 10)
        {
            hasGroup10 = true;
            vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &leadId);
        }
        else if (object.kind == 7 && object.subtype == 7)
        {
            u8 requestType = 0;
            if (vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &requestType) && requestType == 1)
                hasType1 = true;
        }
    }

    if (!hasGroup10)
        return 0;
    u32 pos = 5;
    u8 objectCount = 0;
    if (!vm_net_mock_append_group_info_object(out, outCap, &pos, leadId))
        return 0;
    objectCount += 1;
    if (hasType1 && !vm_net_mock_append_type1_object(out, outCap, &pos))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && enableMiscSync8 && !vm_net_mock_append_misc_player_sync8_object(out, outCap, &pos))
        return 0;
    if (hasType1 && enableMiscSync8)
        objectCount += 1;
    if (hasType1 && !vm_net_mock_append_misc_player_type_object(out, outCap, &pos, 20))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && !vm_net_mock_append_misc_player_type_object(out, outCap, &pos, 32))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && includeRoomNpc && !vm_net_mock_append_scene_room_npc_object(out, outCap, &pos))
        return 0;
    if (hasType1 && includeRoomNpc)
        objectCount += 1;
    if (hasType1 && includeRoomRoles && !vm_net_mock_append_scene_room_roles_object(out, outCap, &pos))
        return 0;
    if (hasType1 && includeRoomRoles)
        objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_group_type1_response leadid=%u objects=%u miscSync8=%u bundledType2Type3=%u sceneEnter=%u sceneRoomNpc=%u sceneRoomRoles=%u bundledSceneFollowup=%u bundledFbTargetClear=%u len=%u\n",
                 leadId,
                 objectCount,
                 (hasType1 && enableMiscSync8) ? 1u : 0u,
                 hasType1 ? 1u : 0u,
                 0u,
                 (hasType1 && includeRoomNpc) ? 1u : 0u,
                 (hasType1 && includeRoomRoles) ? 1u : 0u,
                 0u,
                 includeFbTargetClear ? 1u : 0u,
                 pos);
    return pos;
}

static u32 vm_net_mock_build_scene_change_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_scene_change_target target;
    if (outCap < pos)
        return 0;
    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2, target.scene, target.x, target.y))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    vm_net_trace("mock_scene_change_response scene=%s exitId=%u pos=%u,%u len=%u\n",
                 target.scene, target.exitId, target.x, target.y, pos);
    return pos;
}

static bool vm_net_mock_append_login_tail_skill_objects(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0c, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "learnednum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "learnedskill", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 42, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "booknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "booksinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_books42_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 42, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "booknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "booksinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_empty11_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 11, &objectStart))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_result12_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneKey, u16 spawnX, u16 spawnY)
{
    u8 posInfo[8];
    u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), spawnX, spawnY);
    u32 objectStart = 0;
    if (posInfoLen == 0)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 12, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "fb", 1))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "name", sceneKey))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_result12_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_fb_target_result12_for_scene(out, outCap, pos,
                                                          vm_net_mock_scene_key_name(),
                                                          vm_net_mock_scene_spawn_x(),
                                                          vm_net_mock_scene_spawn_y());
}

static bool vm_net_mock_append_fb_target_result4_object(u8 *out, u32 outCap, u32 *pos,
                                                        u8 typeValue, const char *infoText)
{
    u32 objectStart = 0;
    if (infoText == NULL)
        infoText = "";
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 4, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "min", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", typeValue))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "fb", 1))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "info", infoText))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_scene_change_combo_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    bool needSkill = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    bool needBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    bool needFb11 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11);
    bool needFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    bool splitScenePosCommit = true;
    u8 fb4Type = 1;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_scene_change_target target;
    bool recentCompletedTarget = false;
    if (outCap < pos)
        return 0;
    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    recentCompletedTarget = vm_net_mock_is_recent_completed_scene_change_target(&target);
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 0x1b && object.subtype == 4)
        {
            (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &fb4Type);
            break;
        }
    }
    if (splitScenePosCommit)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    else
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2, target.scene, target.x, target.y))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    if (needFb11)
    {
        if (!splitScenePosCommit)
        {
            if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos, target.scene, target.x, target.y))
                return 0;
            objectCount += 1;
        }
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }
    if (needFb4)
    {
        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos, fb4Type, vm_net_mock_fb_target_info_text()))
            return 0;
        objectCount += 1;
    }
    if (needSkill)
    {
        if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, &pos))
            return 0;
        objectCount += 2;
    }
    else if (needBooks)
    {
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }
    if (!splitScenePosCommit)
    {
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos, target.scene, target.x, target.y))
            return 0;
        objectCount += 1;
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    if (!recentCompletedTarget)
        vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    if (!recentCompletedTarget)
        vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-target");
    g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
    vm_net_trace("mock_scene_change_combo_response objects=%u skill=%u books=%u fb11=%u fb11empty=%u fb12=%u fb4=%u fb4type=%u taskSubset=0 sceneChangeResult=%u trailingSceneEnter=%u deferredSceneCompletion=%u recentCompleted=%u scene=%s exitId=%u pos=%u,%u len=%u\n",
                 objectCount,
                 needSkill ? 1u : 0u,
                 needBooks ? 1u : 0u,
                 needFb11 ? 1u : 0u,
                 needFb11 ? 1u : 0u,
                 (needFb11 && !splitScenePosCommit) ? 1u : 0u,
                 needFb4 ? 1u : 0u,
                 fb4Type,
                 1u,
                 splitScenePosCommit ? 0u : 1u,
                 (!recentCompletedTarget && splitScenePosCommit) ? 1u : 0u,
                 recentCompletedTarget ? 1u : 0u,
                 target.scene,
                 target.exitId,
                 target.x,
                 target.y,
                 pos);
    return pos;
}

static u32 vm_net_mock_build_type27_followup_combo_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    bool needSceneDefault = vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5);
    bool needFb11 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11);
    bool needFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    bool needBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    u8 fb4Type = 1;
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 objectStart = 0;

    if (outCap < pos || !needFb4)
        return 0;

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 0x1b && object.subtype == 4)
        {
            (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &fb4Type);
            break;
        }
    }

    if (needSceneDefault)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x19, 12, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }

    if (needFb11)
    {
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }

    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos, fb4Type, ""))
        return 0;
    objectCount += 1;

    if (needBooks)
    {
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_type27_followup_combo_response objects=%u sceneDefault=%u fb11=%u fb4type=%u books=%u len=%u\n",
                 objectCount, needSceneDefault ? 1u : 0u, needFb11 ? 1u : 0u, fb4Type, needBooks ? 1u : 0u, pos);
    return pos;
}

static u32 vm_net_mock_build_scene_default_event_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    if (outCap < pos)
        return 0;
    if (g_vm_net_mock_last_scene_change_target_valid &&
        g_vm_net_mock_last_scene_change_from_actor_other_portal)
    {
        vm_net_mock_scene_change_target target = g_vm_net_mock_last_scene_change_target;
        if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                               false, true, false, true, false, false))
            return 0;
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                            target.scene, target.x, target.y))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        vm_net_mock_mark_pending_scene_pos_save(target.scene, target.x, target.y, "actor-other-portal-completion");
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        vm_net_trace("mock_scene_default_event_actor_other_portal_completion objects=%u scene=%s pos=%u,%u taskSubset=1 actorOtherPos=0 trailingSceneEnter=1 trailingScenePosResult=0 len=%u\n",
                     objectCount,
                     target.scene,
                     (unsigned int)target.x,
                     (unsigned int)target.y,
                     pos);
        return pos;
    }
    if (g_mockBattleOperateSessionFinished != 0)
    {
        g_mockBattleOperateSessionFinished = 0;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x0a, 0x11, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "lastexp", 1))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "curexp", 1))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        vm_net_mock_finish_wt_packet(out, pos, 1);
        vm_net_trace("mock_scene_default_event_response response=10/17 result=1 battleTerminalAck=1 len=%u evidence=runtime:25_5_result1_still_dispatches_kind25_subtype5_into_Battle_sub17AC_generic_branch,docs/re/battle-flow.md_battle_end_response_cmd10_subcmd17_result1_victory\n",
                     pos);
        return pos;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x19, 5, &objectStart))
        return 0;
    /*
     * scene_runtime_tick() sends the empty 0x19/5 scene-default event request.
     * Repository evidence already showed that a same-subtype `1/25/5 {result=4}`
     * shell is parser-safe on the scene resource-followup path, while the older
     * generic `1/25/12 {result=4}` clear path is consumed globally by
     * net_handle_info_banner_state() and now also correlates with battle exit.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_scene_default_event_response response=25/5 result=4 battleTerminalAck=0 len=%u evidence=scene_resource_followup_same_subtype_safe_shell_and_battle_25_12_global_clear_negative\n", pos);
    return pos;
}

static bool vm_net_mock_is_actor_other_scene_default_combo_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 typeValue = 0;

    if (request == NULL || requestLen != 24)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 10)
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue) || typeValue != 1)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x19 || object.subtype != 5 || object.payloadLen != 0)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_scene_resource_followup_request(const u8 *request, u32 requestLen)
{
    u8 typeValue = 0;
    bool needSkillBooks = false;
    bool needTaskFamily = false;
    if (request == NULL || (requestLen != 49 && requestLen != 39))
        return false;
    needSkillBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
                     vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    if (needSkillBooks &&
        (!vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
         !vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42)))
        return false;
    needTaskFamily = vm_net_mock_request_contains_object(request, requestLen, 1, 6, 1) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 6, 13) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 6, 14) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5);
    if (!needTaskFamily)
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue))
        return false;
    return typeValue == 101;
}

static bool vm_net_mock_is_scene_task_subset_followup_request(const u8 *request, u32 requestLen)
{
    u8 typeValue = 0;
    if (request == NULL || requestLen < 24)
        return false;
    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 6, 1) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 6, 13) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 6, 14) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5))
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue))
        return false;
    return typeValue == 101;
}

static bool vm_net_mock_append_taskinfo_empty1_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "taskinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_tasktypes_empty13_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u32 blobPos = 0;
    u8 blob[64];

    memset(blob, 0, sizeof(blob));
    for (u32 i = 0; i < 6; ++i)
    {
        if (!vm_net_mock_seq_put_u8(blob, sizeof(blob), &blobPos, 0))
            return false;
        if (!vm_net_mock_seq_put_string(blob, sizeof(blob), &blobPos, ""))
            return false;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 13, &objectStart))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "tasktypes", blob, (u16)blobPos))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_taskaction_empty14_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 14, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "action", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "tasknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "taskinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_actor_other_empty10_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "othernum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "otherinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

typedef struct
{
    u32 actorId;
    u16 x;
    u16 y;
    u8 kind;
    const char *actorResource;
    u8 sceneType;
    const char *displayName;
    u8 scriptType;
    const char *scriptName;
} vm_net_mock_scene_npc_seed;

static bool vm_net_mock_scene_is_penglai02(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32") == 0 ||
            strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65") == 0);
}

static bool vm_net_mock_scene_is_penglai04(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34") == 0 ||
            strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65") == 0);
}

static bool vm_net_mock_seq_put_actor_other_scene_npc(u8 *out, u32 outCap, u32 *pos,
                                                      const vm_net_mock_scene_npc_seed *seed)
{
    if (seed == NULL)
        return false;

    return vm_net_mock_seq_put_u32(out, outCap, pos, seed->actorId) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, seed->x) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, seed->y) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, seed->kind) &&
           vm_net_mock_seq_put_string(out, outCap, pos, seed->actorResource) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, seed->sceneType) &&
           vm_net_mock_seq_put_string(out, outCap, pos, seed->displayName) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, seed->scriptType) &&
           vm_net_mock_seq_put_string(out, outCap, pos, seed->scriptName) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, seed->x) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, seed->y);
}

static bool vm_net_mock_append_actor_other_scene_npcs10_object(u8 *out, u32 outCap, u32 *pos,
                                                              const char *scene,
                                                              u32 *npcCountOut,
                                                              u32 *otherInfoLenOut)
{
    static const vm_net_mock_scene_npc_seed penglai02Seeds[] = {
        {20020, 70, 38, 8, "n_swordmaster.actor", 0, "\xc5\xb7\xd2\xb1\xd7\xd3", 0, ""},
        {20021, 108, 38, 8, "e_monkey.actor", 0, "\xd0\xa1\xba\xef\xd7\xd3", 0, ""},
    };
    static const vm_net_mock_scene_npc_seed penglai04Seeds[] = {
        {20011, 166, 280, 4, "n_doctor.actor", 0, "\xd2\xa9\xca\xa6", 0, "task11.xse"},
        {20013, 478, 91, 5, "n_assissonmaster.actor", 3, "\xbe\xf8\xc8\xd0\x2d\xbb\xc3\xbd\xa3", 0, "task13.xse"},
        {20014, 239, 75, 5, "n_magemaster.actor", 4, "\xd2\xa9\xcd\xf5\x2d\xb9\xed\xb5\xc0", 0, "task14.xse"},
    };
    const vm_net_mock_scene_npc_seed *seeds = NULL;
    u32 seedCount = 0;
    u8 otherInfo[1024];
    u32 otherInfoLen = 0;
    u32 objectStart = 0;

    if (npcCountOut)
        *npcCountOut = 0;
    if (otherInfoLenOut)
        *otherInfoLenOut = 0;

    if (vm_net_mock_scene_is_penglai02(scene))
    {
        seeds = penglai02Seeds;
        seedCount = (u32)(sizeof(penglai02Seeds) / sizeof(penglai02Seeds[0]));
    }
    else if (vm_net_mock_scene_is_penglai04(scene))
    {
        seeds = penglai04Seeds;
        seedCount = (u32)(sizeof(penglai04Seeds) / sizeof(penglai04Seeds[0]));
    }

    if (seeds == NULL || seedCount == 0)
        return vm_net_mock_append_actor_other_empty10_object(out, outCap, pos);

    memset(otherInfo, 0, sizeof(otherInfo));
    for (u32 i = 0; i < seedCount; ++i)
    {
        if (!vm_net_mock_seq_put_actor_other_scene_npc(otherInfo, sizeof(otherInfo), &otherInfoLen, &seeds[i]))
            return false;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "othernum", seedCount))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "otherinfo", otherInfo, (u16)otherInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (npcCountOut)
        *npcCountOut = seedCount;
    if (otherInfoLenOut)
        *otherInfoLenOut = otherInfoLen;
    return true;
}

static bool vm_net_mock_append_battle_status7_object(u8 *out, u32 outCap, u32 *pos);

static bool vm_net_mock_is_actor_other_only10_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 10)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_scene_interaction_followup_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen != 61)
        return false;
    return vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 14, 14) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 14, 4) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 14, 5) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 14, 6);
}

static u32 vm_net_mock_build_actor_other_only10_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u8 requestType = 0;
    bool hasType = false;

    if (outCap < pos)
        return 0;
    hasType = vm_net_mock_get_object_u8_field(request, requestLen, "Type", &requestType);
    if (g_vm_net_mock_last_scene_change_target_valid)
    {
        const vm_net_mock_scene_change_target *target = &g_vm_net_mock_last_scene_change_target;
        if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                               false, true, true, true, false, false))
            return 0;
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                             target->scene, target->x, target->y))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        vm_net_mock_mark_pending_scene_pos_save(target->scene, target->x, target->y, "actor-other-portal-deferred-completion");
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        vm_net_trace("mock_actor_other_deferred_scene_response requestType=%s%u objects=%u scene=%s pos=%u,%u fbPrompt=0 trailingSceneEnter=1 len=%u\n",
                     hasType ? "" : "<absent>:",
                     hasType ? (unsigned int)requestType : 0u,
                     objectCount,
                     target->scene,
                     (unsigned int)target->x,
                     (unsigned int)target->y,
                     pos);
        return pos;
    }
    if (hasType && requestType == 100)
    {
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, 1);
        vm_net_trace("mock_actor_other_type100_empty10_response requestType=100 othernum=0 len=%u evidence=runtime:4_7_status7_crash_pc_05189178_lastPc_05189178,Battle.cbm:sub_743C_iteminfo_stream_path hypothesis=Type100_contract_unresolved\n",
                     pos);
        return pos;
    }

    if (hasType && requestType == 1 && vm_net_mock_env_u32("CBE_SCENE_NPC_OTHERINFO", 0) != 0)
    {
        const char *scene = vm_net_mock_current_scene_name();
        u32 npcCount = 0;
        u32 otherInfoLen = 0;

        if (!vm_net_mock_append_actor_other_scene_npcs10_object(out, outCap, &pos,
                                                               scene, &npcCount, &otherInfoLen))
            return 0;
        if (npcCount > 0)
        {
            vm_net_mock_finish_wt_packet(out, pos, 1);
            vm_net_trace("mock_actor_other_scene_npc_response requestType=1 scene=%s othernum=%u otherinfoLen=%u len=%u evidence=scene_local_marker_or_actor_seed,main:sub_1012958_otherinfo_scene_node_find_or_create\n",
                         scene ? scene : "?",
                         (unsigned int)npcCount,
                         (unsigned int)otherInfoLen,
                         pos);
            return pos;
        }
        pos = 5;
    }

    if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_actor_other_only10_response requestType=%s%u othernum=0 len=%u evidence=main:net_handle_actor_move_info kind2/subtype10 parser-safe-empty hypothesis=Type100_battle_followup_unresolved\n",
                 hasType ? "" : "<absent>:",
                 hasType ? (unsigned int)requestType : 0u,
                 pos);
    return pos;
}

static bool vm_net_mock_read_current_player_grid(u32 *nodeOut, u32 *actorIdOut,
                                                 u16 *gridXOut, u16 *gridYOut,
                                                 u16 *targetXOut, u16 *targetYOut)
{
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 actorId = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 targetX = 0;
    u16 targetY = 0;

    if (!Global_R9)
        return false;
    hudState = Global_R9 + 0x5C64;
    if (uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4) != UC_ERR_OK || currentSceneNode == 0)
        return false;
    (void)uc_mem_read(MTK, currentSceneNode + 0x64, &actorId, 4);
    (void)uc_mem_read(MTK, currentSceneNode + 0x18, &gridX, 2);
    (void)uc_mem_read(MTK, currentSceneNode + 0x1A, &gridY, 2);
    (void)uc_mem_read(MTK, currentSceneNode + 0x11E, &targetX, 2);
    (void)uc_mem_read(MTK, currentSceneNode + 0x120, &targetY, 2);

    if ((gridX == 0 || gridY == 0) && targetX != 0 && targetY != 0)
    {
        gridX = targetX;
        gridY = targetY;
    }
    if (gridX == 0 || gridY == 0)
        return false;

    if (nodeOut)
        *nodeOut = currentSceneNode;
    if (actorIdOut)
        *actorIdOut = actorId;
    if (gridXOut)
        *gridXOut = gridX;
    if (gridYOut)
        *gridYOut = gridY;
    if (targetXOut)
        *targetXOut = targetX;
    if (targetYOut)
        *targetYOut = targetY;
    return true;
}

typedef struct
{
    const char *sourceScene;
    u16 left;
    u16 top;
    u16 right;
    u16 bottom;
    const char *targetScene;
    u16 targetX;
    u16 targetY;
    const char *traceName;
} vm_net_mock_portal_fallback;

static bool vm_net_mock_find_portal_fallback(const char *scene, u16 gridX, u16 gridY,
                                             vm_net_mock_scene_change_target *target,
                                             const char **traceNameOut)
{
    static const vm_net_mock_portal_fallback kPortalFallbacks[] = {
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            208, 432, 256, 448,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            80, 60,
            "taohuadao-01-bottom-to-02"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            320, 272, 352, 336,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            40, 70,
            "taohuadao-02-east-to-03"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            32, 0, 128, 48,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            225, 116,
            "taohuadao-02-north-to-01"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            160, 553, 240, 570,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            42, 60,
            "taohuadao-03-bottom-to-04"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            20, 10, 60, 55,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            305, 310,
            "taohuadao-03-north-to-02"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            20, 5, 64, 35,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            200, 540,
            "taohuadao-04-north-to-03"
        },
    };
    size_t i;

    if (scene == NULL || target == NULL)
        return false;

    for (i = 0; i < sizeof(kPortalFallbacks) / sizeof(kPortalFallbacks[0]); ++i)
    {
        const vm_net_mock_portal_fallback *fallback = &kPortalFallbacks[i];
        if (strcmp(scene, fallback->sourceScene) != 0)
            continue;
        if (gridX < fallback->left || gridX > fallback->right ||
            gridY < fallback->top || gridY > fallback->bottom)
            continue;

        memset(target, 0, sizeof(*target));
        snprintf(target->scene, sizeof(target->scene), "%s", fallback->targetScene);
        target->x = fallback->targetX;
        target->y = fallback->targetY;
        if (traceNameOut)
            *traceNameOut = fallback->traceName;
        return true;
    }

    return false;
}

static bool vm_net_mock_find_portal_fallback_margin(const char *scene, u16 gridX, u16 gridY,
                                                    u16 margin,
                                                    vm_net_mock_scene_change_target *target,
                                                    const char **traceNameOut)
{
    static const vm_net_mock_portal_fallback kPortalFallbacks[] = {
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            208, 432, 256, 448,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            80, 60,
            "taohuadao-01-bottom-to-02"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            320, 272, 352, 336,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            40, 70,
            "taohuadao-02-east-to-03"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            32, 0, 128, 48,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            225, 116,
            "taohuadao-02-north-to-01"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            160, 553, 240, 570,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            42, 60,
            "taohuadao-03-bottom-to-04"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            20, 10, 60, 55,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            305, 310,
            "taohuadao-03-north-to-02"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            20, 5, 64, 35,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            200, 540,
            "taohuadao-04-north-to-03"
        },
    };
    size_t i;

    if (scene == NULL || target == NULL)
        return false;

    for (i = 0; i < sizeof(kPortalFallbacks) / sizeof(kPortalFallbacks[0]); ++i)
    {
        const vm_net_mock_portal_fallback *fallback = &kPortalFallbacks[i];
        u16 left = (fallback->left > margin) ? (u16)(fallback->left - margin) : 0;
        u16 top = (fallback->top > margin) ? (u16)(fallback->top - margin) : 0;
        u16 right = (u16)(fallback->right + margin);
        u16 bottom = (u16)(fallback->bottom + margin);
        if (strcmp(scene, fallback->sourceScene) != 0)
            continue;
        if (gridX < left || gridX > right || gridY < top || gridY > bottom)
            continue;

        memset(target, 0, sizeof(*target));
        snprintf(target->scene, sizeof(target->scene), "%s", fallback->targetScene);
        target->x = fallback->targetX;
        target->y = fallback->targetY;
        if (traceNameOut)
            *traceNameOut = fallback->traceName;
        return true;
    }

    return false;
}

static u32 vm_net_mock_build_actor_other_portal_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    const char *scene = NULL;
    char sceneForTrace[64];
    char pendingScene[96];
    char pendingSceneRaw[96];
    vm_net_mock_scene_change_target target;
    const char *portalTraceName = NULL;
    u16 gridX = 0;
    u16 gridY = 0;
    u32 sceneObj = 0;

    if (outCap < pos || !vm_net_mock_is_actor_other_only10_request(request, requestLen))
        return 0;

    pendingScene[0] = 0;
    pendingSceneRaw[0] = 0;
    if (!vm_net_mock_read_current_player_grid(NULL, NULL, &gridX, &gridY, NULL, NULL))
        return 0;
    scene = vm_net_mock_current_scene_name();
    snprintf(sceneForTrace, sizeof(sceneForTrace), "%s", scene ? scene : "");
    if (Global_R9)
        (void)uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
    if (sceneObj)
    {
        vm_net_read_guest_best_effort_label(sceneObj + 0x475, pendingScene, sizeof(pendingScene));
        (void)vm_net_read_guest_raw_cstr(sceneObj + 0x475, pendingSceneRaw, sizeof(pendingSceneRaw));
    }

    if (!vm_net_mock_find_portal_fallback(scene, gridX, gridY, &target, &portalTraceName))
    {
        return 0;
    }

    if (pendingSceneRaw[0] != 0 && strcmp(pendingSceneRaw, target.scene) == 0 &&
        !(g_vm_net_mock_pending_scene_save_valid &&
          strcmp(g_vm_net_mock_pending_scene_save_scene, target.scene) == 0))
    {
        u32 objectStart = 0;
        vm_net_mock_remember_scene_change_target(&target);
        g_vm_net_mock_last_scene_change_from_actor_other_portal = true;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        vm_net_trace("mock_actor_other_local_table_split_scene_ack request=2/10 portal=%s scene=%s pos=%u,%u pendingScene=%s pendingRawMatch=1 target=%s targetPos=%u,%u response=30/2 result=1 type=2 noPos objects=%u len=%u\n",
                     portalTraceName ? portalTraceName : "<unknown>",
                     sceneForTrace,
                     (unsigned int)gridX,
                     (unsigned int)gridY,
                     pendingScene,
                     target.scene,
                     (unsigned int)target.x,
                     (unsigned int)target.y,
                     objectCount,
                     pos);
        return pos;
    }

    /*
     * IDA/runtime evidence: these same-class edge portals are already present
     * in the local scene table consumed by sub_1018166(). When the player steps
     * inside such a rect, the client emits this 2/10 Type=1 request as part of
     * its local transition countdown. Returning a server-forced 30/2 here closes
     * the business dispatch gate before that local transition can finish.
     */
    vm_net_trace("mock_actor_other_portal_local_table_passthrough request=2/10 portal=%s scene=%s pos=%u,%u pendingScene=%s pendingRawMatch=%u target=%s targetPos=%u,%u response=empty-otherinfo\n",
                 portalTraceName ? portalTraceName : "<unknown>",
                 sceneForTrace,
                 (unsigned int)gridX,
                 (unsigned int)gridY,
                 pendingScene[0] ? pendingScene : "<empty>",
                 (unsigned int)(pendingSceneRaw[0] != 0 && strcmp(pendingSceneRaw, target.scene) == 0),
                 target.scene,
                 (unsigned int)target.x,
                 (unsigned int)target.y);
    return 0;
}

static bool vm_net_mock_is_actor_moveinfo_upload_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 1)
        return false;
    if (!vm_net_mock_request_contains(request, requestLen, "moveinfo"))
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_actor_moveinfo_name_update_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 12)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_actor_moveinfo_name_update_response(const u8 *request, u32 requestLen,
                                                                u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    const char *name = vm_net_mock_scene_key_name();

    (void)request;
    (void)requestLen;
    if (outCap < pos || !vm_net_mock_is_actor_moveinfo_name_update_request(request, requestLen))
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 2, 12, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", name ? name : ""))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_actor_moveinfo_name_update_response scene=%s len=%u\n",
                 name ? name : "",
                 pos);
    return pos;
}

static bool vm_net_mock_append_actor_moveinfo_empty_ack_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 1, &objectStart))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_snapshot_current_player_pos(const char *reason)
{
    u32 currentSceneNode = 0;
    u32 actorId = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 targetX = 0;
    u16 targetY = 0;

    if (!vm_net_mock_read_current_player_grid(&currentSceneNode, &actorId,
                                              &gridX, &gridY, &targetX, &targetY))
        return false;

    vm_net_mock_save_player_pos_state(NULL, gridX, gridY, reason);
    vm_net_trace("mock_player_pos_snapshot reason=%s node=%08x actor=%u grid=%u,%u target=%u,%u scene=%s\n",
                 reason ? reason : "?",
                 currentSceneNode,
                 actorId,
                 (unsigned int)gridX,
                 (unsigned int)gridY,
                 (unsigned int)targetX,
                 (unsigned int)targetY,
                 vm_net_mock_current_scene_name());
    return true;
}

static u32 vm_net_mock_build_actor_moveinfo_ack_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    const u8 *moveInfo = NULL;
    u16 moveInfoLen = 0;
    bool snappedPos = false;
    const char *scene = NULL;
    char sceneForTrace[64];
    const char sourceScene01[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_01.sce */
    const char targetScene01[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_02.sce */
    const char sourceScene03[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_03.sce */
    const char targetScene03[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_04.sce */
    u16 gridX = 0;
    u16 gridY = 0;
    vm_net_mock_scene_change_target portalTarget;
    const char *portalTraceName = NULL;

    if (outCap < pos || !vm_net_mock_is_actor_moveinfo_upload_request(request, requestLen))
        return 0;
    (void)vm_net_mock_get_object_blob_field(request, requestLen, "moveinfo", &moveInfo, &moveInfoLen);
    snappedPos = vm_net_mock_snapshot_current_player_pos("moveinfo-upload");
    scene = vm_net_mock_current_scene_name();
    snprintf(sceneForTrace, sizeof(sceneForTrace), "%s", scene ? scene : "");
    gridX = vm_net_mock_scene_spawn_x();
    gridY = vm_net_mock_scene_spawn_y();

    /*
     * 2026-06-09 runtime evidence: the first Taohuadao bottom exit can stop at
     * y=410 while the local SCE trigger rect starts at y=432, so the client
     * never emits the later 2/3 or 4/1 scene-change request. Treat the narrow
     * approach band as the server-side completion for this known portal only.
     */
    if (snappedPos &&
        strcmp(scene, sourceScene01) == 0 &&
        gridX >= 208 && gridX <= 256 &&
        gridY >= 408 && gridY <= 448)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 1, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, false, false, 0, targetScene01, 80, 60))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        vm_net_mock_finish_wt_packet(out, pos, 1);
        vm_net_mock_save_player_pos_state(targetScene01, 80, 60, "moveinfo-taohuadao-bottom-portal");
        vm_net_trace("mock_actor_moveinfo_portal_response scene=%s pos=%u,%u target=%s targetPos=80,60 moveinfoLen=%u len=%u\n",
                     sceneForTrace,
                     (unsigned int)gridX,
                     (unsigned int)gridY,
                     targetScene01,
                     (unsigned int)moveInfoLen,
                     pos);
        return pos;
    }

    if (snappedPos &&
        strcmp(scene, sourceScene03) == 0 &&
        gridX >= 160 && gridX <= 240 &&
        gridY >= 553 && gridY <= 570)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 1, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, false, false, 0, targetScene03, 42, 60))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        vm_net_mock_finish_wt_packet(out, pos, 1);
        vm_net_mock_save_player_pos_state(targetScene03, 42, 60, "moveinfo-taohuadao-bottom-portal");
        vm_net_trace("mock_actor_moveinfo_portal_response_bottom_03 scene=%s pos=%u,%u target=%s targetPos=42,60 moveinfoLen=%u len=%u\n",
                     sceneForTrace,
                     (unsigned int)gridX,
                     (unsigned int)gridY,
                     targetScene03,
                     (unsigned int)moveInfoLen,
                     pos);
        return pos;
    }

    if (snappedPos &&
        vm_net_mock_find_portal_fallback_margin(scene, gridX, gridY, 8,
                                                &portalTarget, &portalTraceName))
    {
        /*
         * Local-table same-class portals already drive sub_1018166() through a
         * 30-frame countdown. A moveinfo-side split ack in this margin window
         * races that local path, sets a pending scene save early, and prevents
         * the later pendingRawMatch=1 2/10 completion from being exercised.
         */
        vm_net_trace("mock_actor_moveinfo_portal_local_table_passthrough portal=%s scene=%s pos=%u,%u target=%s targetPos=%u,%u margin=8 moveinfoLen=%u response=empty-moveinfo-ack\n",
                     portalTraceName ? portalTraceName : "<unknown>",
                     sceneForTrace,
                     (unsigned int)gridX,
                     (unsigned int)gridY,
                     portalTarget.scene,
                     (unsigned int)portalTarget.x,
                     (unsigned int)portalTarget.y,
                     (unsigned int)moveInfoLen);
    }

    /*
     * net_handle_actor_move_info() only reads moveinfo for subtype 2. Subtype 1
     * falls through the default branch at 0x01012B0C, so keep this upload ack
     * empty until the live server semantics are recovered.
     */
    if (!vm_net_mock_append_actor_moveinfo_empty_ack_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_actor_moveinfo_ack_response subtype=1 empty moveinfoLen=%u first=%02x,%02x,%02x,%02x len=%u\n",
                 (unsigned int)moveInfoLen,
                 moveInfoLen > 0 ? moveInfo[0] : 0,
                 moveInfoLen > 1 ? moveInfo[1] : 0,
                 moveInfoLen > 2 ? moveInfo[2] : 0,
                 moveInfoLen > 3 ? moveInfo[3] : 0,
                 pos);
    return pos;
}

static bool vm_net_mock_is_challenge_interaction_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    bool hasChallenge = false;
    bool hasMoveinfo = false;

    if (request == NULL || requestLen < 9)
        return false;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 4 && object.subtype == 1)
        {
            if (hasChallenge)
                return false;
            hasChallenge = true;
        }
        else if (object.major == 1 && object.kind == 2 && object.subtype == 1)
        {
            if (hasMoveinfo)
                return false;
            hasMoveinfo = true;
        }
        else
        {
            return false;
        }
    }
    return offset == requestLen &&
           hasChallenge &&
           vm_net_mock_request_contains(request, requestLen, "id") &&
           vm_net_mock_request_contains(request, requestLen, "index") &&
           vm_net_mock_request_contains(request, requestLen, "posx") &&
           vm_net_mock_request_contains(request, requestLen, "posy");
}

static u32 vm_net_mock_build_battle_start_info_blob(u8 *out, u32 outCap, u32 roleId, u32 enemyId)
{
    u32 pos = 0;
    const char roleName[] = "\xcf\xc0\xbd\xa3\xbd\xad\xba\xfe"; /* GBK: xia jian jiang hu */
    u32 roleHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", 120);
    u32 roleMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_HP", roleHp);
    u32 roleMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MP", 40);
    u32 roleMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_MP", roleMp);
    u32 enemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", 20);
    u32 enemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", enemyHp);
    u32 enemyMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", 20);
    u32 enemyMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_MP", enemyMp);

    if (roleId == 0)
        roleId = 10001;
    if (enemyId == 0)
        enemyId = 105;

    /*
     * Battle.cbm sub_66CC subtype 10 reads:
     * u8 ownCount, repeated own(id,u32,u32,u32,u32,name,u8,u8),
     * u8 enemyCount, repeated enemy(id,u32,u32,u32,u32).
     * The trailing own visual bytes are passed as sub_23F6(second, first);
     * first=0, second=1 avoids the negative table index used by (0,0).
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleMaxHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleMp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleMaxMp))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, enemyId))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, enemyHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, enemyMaxHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, enemyMp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, enemyMaxMp))
        return 0;
    return pos;
}

static u32 vm_net_mock_resolve_battle_enemy_id(u32 requestedId, u32 *tableBaseOut, u32 tableIds[4])
{
    u32 loaderR9 = vm_screen_stack_lookup_module_base(vmAddedScreen);
    u32 battleBase = 0;
    u32 enemyTable = 0;
    u32 firstNonzero = 0;

    if (tableBaseOut)
        *tableBaseOut = 0;
    if (tableIds)
        memset(tableIds, 0, sizeof(u32) * 4);
    if (loaderR9 == 0)
        loaderR9 = g_currentScreenModuleBase;
    if (loaderR9 == 0)
        return requestedId;

    battleBase = loaderR9 + 0x3450u;
    if (uc_mem_read(MTK, battleBase + 0x50u, &enemyTable, 4) != UC_ERR_OK || enemyTable == 0)
        return requestedId;
    if (tableBaseOut)
        *tableBaseOut = enemyTable;
    vm_net_trace_encounter_table_records("battle_resolve_enemy_table", enemyTable);

    for (u32 i = 0; i < 4; ++i)
    {
        u32 id = 0;
        (void)uc_mem_read(MTK, enemyTable + i * 0x4Cu + 0x24u, &id, 4);
        if (tableIds)
            tableIds[i] = id;
        if (id == requestedId)
            return requestedId;
        if (firstNonzero == 0 && id != 0)
            firstNonzero = id;
    }

    /*
     * A request id that is not already present in Battle.cbm's enemy template
     * table is not render-safe: sub_66CC takes the lookup-failed path and the
     * later fighter draw loop calls a zero callback. Keep the table id as the
     * default crash-safe server contract; force the request id only for focused
     * experiments while recovering the upstream template-population contract.
     */
    if (requestedId != 0 && vm_net_mock_env_u32("CBE_BATTLE_FORCE_REQUEST_ENEMY_ID", 0) != 0)
        return requestedId;
    return firstNonzero ? firstNonzero : requestedId;
}

static bool vm_net_mock_is_battle_operate_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 4 &&
           object.subtype == 2 &&
           vm_net_mock_request_contains(request, requestLen, "index") &&
           vm_net_mock_request_contains(request, requestLen, "Operate");
}

static bool vm_net_mock_is_battle_operate_request_relaxed(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return object.major == 1 &&
           object.kind == 4 &&
           object.subtype == 2;
}

static bool vm_net_mock_current_screen_is_battle(void)
{
    u32 inferredCodeBase = 0;
    u32 inferredModuleR9 = 0;

    if (vmAddedScreen != 0 &&
        vm_infer_battle_module_from_screen(vmAddedScreen, &inferredCodeBase, &inferredModuleR9))
        return true;
    if (g_currentScreenThis != 0)
    {
        u32 screen = g_currentScreenThis + 0x18u;
        if (vm_infer_battle_module_from_screen(screen, &inferredCodeBase, &inferredModuleR9))
            return true;
    }
    return false;
}

static bool vm_net_mock_append_battle_actioninfo_record(u8 *actionInfo, u32 actionInfoCap,
                                                        u32 *actionInfoLen, u8 actionType,
                                                        u8 mappedActorWireSlot,
                                                        u8 mappedTargetWireSlot,
                                                        u8 childFlag, u32 valueA,
                                                        u32 valueBSeed, u32 effectIndex,
                                                        u8 tail0, u8 tail1, u8 tail2)
{
    char valueText[16];
    const char *blobText = "";

    if (actionType == 1)
    {
        snprintf(valueText, sizeof(valueText), "%u", valueA);
        blobText = valueText;
    }

    return vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, actionType) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, mappedActorWireSlot) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, 1) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, mappedTargetWireSlot) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, childFlag) &&
           vm_net_mock_seq_put_u32(actionInfo, actionInfoCap, actionInfoLen, valueA) &&
           vm_net_mock_seq_put_u32(actionInfo, actionInfoCap, actionInfoLen, valueBSeed) &&
           vm_net_mock_seq_put_u32(actionInfo, actionInfoCap, actionInfoLen, effectIndex) &&
           vm_net_mock_seq_put_string(actionInfo, actionInfoCap, actionInfoLen, blobText) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, tail0) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, tail1) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, tail2);
}

static bool vm_net_mock_put_battle_action_companion_fields(u8 *out, u32 outCap, u32 *pos)
{
    (void)out;
    (void)outCap;
    (void)pos;
    /*
     * Battle.cbm subtype-6 statically reads iteminfo and teaminfo before
     * actioninfo/actionnum. Runtime negatives showed that raw empty fields,
     * raw tagged-empty fields, and empty WT blob fields all make the item/team
     * wrappers read through neighboring object metadata. Static sub_6DBC also
     * shows that a present teaminfo field is consumed as N current-team records,
     * not as an optional zero-record stream. Omit these fields until the real
     * record grammar is recovered.
     */
    return true;
}

static u32 vm_net_mock_build_battle_operate_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 index = 0;
    u32 operate = 0;
    u8 index8 = 0;
    u8 operate8 = 0;
    u8 actionInfo[128];
    u32 actionInfoLen = 0;
    u8 actorSlot = 1;
    u8 enemySlot = 0;
    u8 firstRecordWireActorUsed = 0;
    u8 firstRecordWireTargetUsed = 0;
    u32 attackDamageValue = 12;
    u32 counterDamageValue = 10;
    u8 actionCount = 1;
    bool bundleWholeRound = false;
    u8 firstRecordActorWireSlot = 1;
    u8 firstRecordChildFlag = 0;
    u32 firstRecordValueB = 0;
    u32 counterRecordValueB = 0;
    bool type1ValueBRemainingHp = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_VALUEB_REMAINING_HP", 1) != 0;
    bool battleEndsThisRound = false;
    bool allowCounterattack = false;
    bool terminalFollowup = false;
    u8 actionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_ACTION_TYPE", 1);
    u8 firstActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_ACTION_TYPE", actionType);
    u8 counterActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTION_TYPE", actionType);
    bool type1BundleCounter = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_BUNDLE_COUNTER", 0) != 0;
    u32 type1EffectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    u8 type1Tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    u8 type1Tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    u8 type1Tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

    if (outCap < pos || !vm_net_mock_is_battle_operate_request(request, requestLen))
    {
        if (vm_net_mock_is_battle_operate_request_relaxed(request, requestLen))
            vm_net_trace("mock_battle_operate_response reject=strict_match_failed len=%u evidence=runtime:request_is_4_2_but_full_field_match_failed\n",
                         requestLen);
        return 0;
    }
    if (!vm_net_mock_get_object_u32_field(request, requestLen, "index", &index) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "index", &index8))
        index = index8;
    if (!vm_net_mock_get_object_u32_field(request, requestLen, "Operate", &operate) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "Operate", &operate8))
        operate = operate8;

    /*
     * Static/runtime Battle.cbm evidence now converges on one stronger claim:
     * - subtype-6 field "actioninfo" is not consumed as a naked byte array.
     * - the header reads at 0x05188E58/0x05188E64/0x05188E7A use the reader
     *   callback at reader+0x28, and runtime cross-reference shows that slot
     *   resolves to 0x01033AAD -> stream_read_i8_tagged.
     * - similarly, the child/value reads later in sub_6EB0 use the reader
     *   callback table's tagged u32/u16/string helpers, not raw direct bytes.
     *
     * Therefore the next parser-faithful experiment is to keep subtype-6 and
     * the same two-record player/enemy round shell, but encode the inner stream
     * with the repository's existing tagged sequence helpers instead of the old
     * hand-written raw 43-byte layout.
     */
    actorSlot = (u8)(index & 0xFFu);
    terminalFollowup = (g_mockBattleOperateSessionFinished != 0);
    if (terminalFollowup)
        bundleWholeRound = false;
    else
        bundleWholeRound = (g_mockBattleOperateSessionArmed != 0 &&
                            operate == 0);
    firstRecordActorWireSlot = (index == 0) ? 2u : actorSlot;
    firstRecordChildFlag = (index == 0) ? 1u : 0u;
    if (!terminalFollowup && g_mockBattleEnemyHpCurrent == 0)
        g_mockBattleEnemyHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", 20);
    if (!terminalFollowup && g_mockBattleRoleHpCurrent == 0)
        g_mockBattleRoleHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", 120);
    if (terminalFollowup)
    {
        attackDamageValue = 0;
        counterDamageValue = 0;
        actionCount = 0;
    }
    else
    {
        attackDamageValue = vm_net_mock_min_u32(12u, g_mockBattleEnemyHpCurrent);
        if (attackDamageValue == 0)
            attackDamageValue = 1;
        if (g_mockBattleEnemyHpCurrent >= attackDamageValue)
            g_mockBattleEnemyHpCurrent -= attackDamageValue;
        else
            g_mockBattleEnemyHpCurrent = 0;
        allowCounterattack = bundleWholeRound && g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0;
        if (firstActionType == 1 && !type1BundleCounter)
            allowCounterattack = false;
        if (allowCounterattack)
        {
            counterDamageValue = vm_net_mock_min_u32(10u, g_mockBattleRoleHpCurrent);
            if (counterDamageValue == 0)
                counterDamageValue = 1;
            if (g_mockBattleRoleHpCurrent >= counterDamageValue)
                g_mockBattleRoleHpCurrent -= counterDamageValue;
            else
                g_mockBattleRoleHpCurrent = 0;
        }
        battleEndsThisRound = (g_mockBattleEnemyHpCurrent == 0 || g_mockBattleRoleHpCurrent == 0);
    }
    if (!terminalFollowup && type1ValueBRemainingHp)
    {
        if (firstActionType == 1)
            firstRecordValueB = g_mockBattleEnemyHpCurrent;
        if (counterActionType == 1)
            counterRecordValueB = g_mockBattleRoleHpCurrent;
    }
    firstRecordValueB = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_B", firstRecordValueB);
    counterRecordValueB = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", counterRecordValueB);
    if (terminalFollowup)
    {
        if (!vm_net_mock_append_battle_status7_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, 4);
        vm_net_trace("mock_battle_operate_response response=4/7+4/8+4/11+4/9 terminalFollowup=1 result=1 case8_result=1 case8_autorevive=1 case11_result=1 case11_type=1 case9_result=1 roleHp=%u/%u enemyHp=%u/%u session=%u turn=%u len=%u evidence=runtime:latest_inline_4_7_4_11_4_9_passes_subtype9_gate_but_stays_on_Battle_auto_prompt,runtime:prior_4_7_4_8_reaches_case8_apply_call_05189DB4_and_sub_4B70_send25_call,runtime:prior_4_7_4_8_4_11_still_consumes_case11_and_writes_mainObj_0x474_1_at_pc_05189B66,Battle.cbm:sub_7BD0_subtype8_requires_mode2_before_apply_Battle_cbm_05189D16_05189DB4,Battle.cbm:sub_7BD0_subtype9_result1_gate_checks_mainObj_0x474_or_0x470_before_05189C04\n",
                     g_mockBattleRoleHpCurrent,
                     g_mockBattleRoleHpMax,
                     g_mockBattleEnemyHpCurrent,
                     g_mockBattleEnemyHpMax,
                     g_mockBattleOperateSessionSerial,
                     g_mockBattleOperateTurnCounter,
                     pos);
        return pos;
    }
    memset(actionInfo, 0, sizeof(actionInfo));
    /*
     * Latest negative evidence (2026-06-12 rerun):
     * - swapping raw slots makes Battle materialize record=2,1,1
     * - that enters 0x05188FCA..0x05188FD0 where [record+2] == activeSlot
     * - the current mock context still has a null owner there, so the client
     *   crashes at 0x05188FD0 before any safe follow-up can occur
     *
     * Keep the pre-swap raw mapping for now. It is still the only confirmed
     * no-crash shape while we recover the remaining actor/target semantics and
     * nonzero value fields.
     */
    /*
     * Runtime evidence on the stable side=0 build:
     * - player turn request index=1 serializes raw actor byte 1
     * - Battle later materializes that into record arg byte 0
     * - monster auto-turn request index=0 currently serializes raw actor byte 0
     *   and still materializes arg byte 0, so it never becomes the opposing actor
     *
     * Narrow next experiment:
     * - for the auto monster-turn window only, bump the raw actor byte to 2
     * - keep the raw target byte at 0 and all other fields unchanged
     * - goal is to see whether Battle now materializes actor arg 1 while
     *   preserving the current parser-safe single-record family
    */
    {
        u8 mappedActorWireSlot = firstRecordActorWireSlot;
        u8 mappedTargetWireSlot = enemySlot;

        if (firstActionType == 1)
        {
            mappedActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_FIRST_ACTOR_WIRE_SLOT", 1);
            mappedTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_FIRST_TARGET_WIRE_SLOT", 0);
        }
        firstRecordWireActorUsed = mappedActorWireSlot;
        firstRecordWireTargetUsed = mappedTargetWireSlot;

        /* record 0 stays on the current live no-crash baseline. */
        if (!terminalFollowup)
        {
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, firstActionType,
                                                             mappedActorWireSlot,
                                                             mappedTargetWireSlot,
                                                             firstRecordChildFlag, attackDamageValue,
                                                             firstRecordValueB,
                                                             firstActionType == 1 ? type1EffectIndex : 0,
                                                             firstActionType == 1 ? type1Tail0 : 0,
                                                             firstActionType == 1 ? type1Tail1 : 0,
                                                             firstActionType == 1 ? type1Tail2 : 0))
                return 0;
        }

        /*
         * Narrow 2026-06-12 follow-up:
         * - Battle.cbm `0x05189018..0x05189022` confirms subtype-6 loops over
         *   `actionnum`, so a two-record packet is parser-valid.
         * - current single-record auto-turn (`index=0`) safely materializes as
         *   `record=2,2,1 valueA=12 valueB=0`, but when sent alone it stalls in
         *   `0x05186B44..0x05186C08` store-target/copy-target.
         * - current user-visible issue is "player hits monster but no counterattack".
         *
         * Latest packet evidence on the current branch:
         * - every live battle attack request still arrives as `4/2 index=0 operate=0`
         * - first request in session returns `actionnum=2` and user sees counterattack
         * - second request regresses to `actionnum=1` and counterattack disappears
         *
         * Therefore bundle the safe second record for every armed `operate=0`
         * attack request, not just the first tracked turn.
         */
        if (allowCounterattack)
        {
            u8 counterActorWireSlot = 2u;
            if (counterActionType == 1)
                counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT", 0);
            actionCount = 2;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, counterActionType,
                                                             counterActorWireSlot, 1u, 1u,
                                                             counterDamageValue, counterRecordValueB,
                                                             counterActionType == 1 ? type1EffectIndex : 0,
                                                             counterActionType == 1 ? type1Tail0 : 0,
                                                             counterActionType == 1 ? type1Tail1 : 0,
                                                             counterActionType == 1 ? type1Tail2 : 0))
                return 0;
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, 6, &objectStart))
        return 0;
    if (!vm_net_mock_put_battle_action_companion_fields(out, outCap, &pos))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "actionnum", actionCount))
        return 0;
    if (!vm_net_mock_put_object_raw(out, outCap, &pos, "actioninfo",
                                    actionInfo, (u16)actionInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    if (battleEndsThisRound)
    {
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 1;
    }
    vm_net_trace_bytes("mock_battle_operate_actioninfo_payload", actionInfo, actionInfoLen);
    vm_net_trace("mock_battle_operate_response response=%s actionnum=%u actioninfoLen=%u actioninfoField=raw companionFields=omitted_until_teaminfo_record_grammar encoding=tagged_seq firstRecordType=%u counterRecordType=%u actorSlots=%u wireMapped=%u_to_%u index=%u operate=%u childFlag=%u damageValue=%u firstValueB=%u counterDamageValue=%u counterValueB=%u type1EffectIndex=%u type1Tail=%u,%u,%u roleHp=%u/%u enemyHp=%u/%u session=%u turn=%u bundleWholeRound=%u type1BundleCounter=%u valueBRemainingHp=%u battleEnds=%u terminalFollowup=%u len=%u evidence=runtime:latest_type1_single_record_no_crash_reaches_damage_dispatch_but_enemy0_stays_20_20_with_valueB0,Battle.cbm:sub_6EB0_materializes_type1_valueA_valueB_fields_05188f96_05188fbc,Battle.cbm:sub_6D12_reads_iteminfo_before_actioninfo,Battle.cbm:sub_6DBC_reads_teaminfo_before_actioninfo,Battle.cbm:sub_6DBC_present_teaminfo_consumes_current_team_record_count_not_zero_record_stream,Battle.cbm:sub_6EB0_header_reads_use_stream_read_i8_tagged_01033AAD_and_child_values_use_tagged_reader_table_callbacks,Battle.cbm:sub_4582_0x4582_type2_only_effect_while_type1_reads_damage_popup_fields,Battle.cbm:0x05189018_0x05189022_loops_over_actionnum_and_reenters_0x05188e30,Battle.cbm:sub_7BD0_subtype9_result1_gate_checks_mainObj_0x474_or_0x470_before_05189C04\n",
                  battleEndsThisRound ? "4/6(lethal-local-ko-pending)" : "4/6",
                  (unsigned int)actionCount,
                  actionInfoLen,
                  (unsigned int)firstActionType,
                  (unsigned int)counterActionType,
                 (unsigned int)actorSlot,
                 (unsigned int)firstRecordWireActorUsed,
                 (unsigned int)firstRecordWireTargetUsed,
                 (unsigned int)index,
                 (unsigned int)operate,
                 (unsigned int)firstRecordChildFlag,
                 attackDamageValue,
                 firstRecordValueB,
                 counterDamageValue,
                 counterRecordValueB,
                 type1EffectIndex,
                 (unsigned int)type1Tail0,
                 (unsigned int)type1Tail1,
                 (unsigned int)type1Tail2,
                 g_mockBattleRoleHpCurrent,
                 g_mockBattleRoleHpMax,
                 g_mockBattleEnemyHpCurrent,
                 g_mockBattleEnemyHpMax,
                 g_mockBattleOperateSessionSerial,
                 g_mockBattleOperateTurnCounter,
                 bundleWholeRound ? 1u : 0u,
                 type1BundleCounter ? 1u : 0u,
                 type1ValueBRemainingHp ? 1u : 0u,
                 battleEndsThisRound ? 1u : 0u,
                 terminalFollowup ? 1u : 0u,
                 pos);
    return pos;
}

static u32 vm_net_mock_build_battle_operate_response_fallback(const u8 *request, u32 requestLen,
                                                              u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 index = 1;
    u32 operate = 0;
    u8 index8 = 1;
    u8 operate8 = 0;
    u8 actionInfo[128];
    u32 actionInfoLen = 0;
    u8 requestKind = 0;
    u8 requestSubtype = 0;
    u8 actorSlot = 1;
    const u8 enemySlot = 0;
    u8 firstRecordWireActorUsed = 0;
    u8 firstRecordWireTargetUsed = 0;
    u32 attackDamageValue = 12;
    u32 counterDamageValue = 10;
    u8 actionCount = 1;
    bool bundleWholeRound = false;
    u8 firstRecordActorWireSlot = 1;
    u8 firstRecordChildFlag = 0;
    u32 firstRecordValueB = 0;
    u32 counterRecordValueB = 0;
    bool type1ValueBRemainingHp = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_VALUEB_REMAINING_HP", 1) != 0;
    bool battleEndsThisRound = false;
    bool allowCounterattack = false;
    bool terminalFollowup = false;
    u8 actionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_ACTION_TYPE", 1);
    u8 firstActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_ACTION_TYPE", actionType);
    u8 counterActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTION_TYPE", actionType);
    bool type1BundleCounter = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_BUNDLE_COUNTER", 0) != 0;
    u32 type1EffectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    u8 type1Tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    u8 type1Tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    u8 type1Tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_is_battle_operate_request_relaxed(request, requestLen))
    {
        if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &requestKind, &requestSubtype) ||
            requestKind != 4 || requestSubtype != 2)
            return 0;
        vm_net_trace("mock_battle_operate_response_fallback accept=header_only len=%u evidence=runtime:4_2_request_reached_unhandled_exit_and_relaxed_object_detector_still_failed\n",
                     requestLen);
    }

    if (!vm_net_mock_get_object_u32_field(request, requestLen, "index", &index) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "index", &index8))
        index = index8;
    if (!vm_net_mock_get_object_u32_field(request, requestLen, "Operate", &operate) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "Operate", &operate8))
        operate = operate8;

    actorSlot = (u8)(index & 0xFFu);
    terminalFollowup = (g_mockBattleOperateSessionFinished != 0);
    if (terminalFollowup)
        bundleWholeRound = false;
    else
        bundleWholeRound = (g_mockBattleOperateSessionArmed != 0 &&
                            operate == 0);
    firstRecordActorWireSlot = (index == 0) ? 2u : actorSlot;
    firstRecordChildFlag = (index == 0) ? 1u : 0u;
    if (!terminalFollowup && g_mockBattleEnemyHpCurrent == 0)
        g_mockBattleEnemyHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", 20);
    if (!terminalFollowup && g_mockBattleRoleHpCurrent == 0)
        g_mockBattleRoleHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", 120);
    if (terminalFollowup)
    {
        attackDamageValue = 0;
        counterDamageValue = 0;
        actionCount = 0;
    }
    else
    {
        attackDamageValue = vm_net_mock_min_u32(12u, g_mockBattleEnemyHpCurrent);
        if (attackDamageValue == 0)
            attackDamageValue = 1;
        if (g_mockBattleEnemyHpCurrent >= attackDamageValue)
            g_mockBattleEnemyHpCurrent -= attackDamageValue;
        else
            g_mockBattleEnemyHpCurrent = 0;
        allowCounterattack = bundleWholeRound && g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0;
        if (firstActionType == 1 && !type1BundleCounter)
            allowCounterattack = false;
        if (allowCounterattack)
        {
            counterDamageValue = vm_net_mock_min_u32(10u, g_mockBattleRoleHpCurrent);
            if (counterDamageValue == 0)
                counterDamageValue = 1;
            if (g_mockBattleRoleHpCurrent >= counterDamageValue)
                g_mockBattleRoleHpCurrent -= counterDamageValue;
            else
                g_mockBattleRoleHpCurrent = 0;
        }
        battleEndsThisRound = (g_mockBattleEnemyHpCurrent == 0 || g_mockBattleRoleHpCurrent == 0);
    }
    if (!terminalFollowup && type1ValueBRemainingHp)
    {
        if (firstActionType == 1)
            firstRecordValueB = g_mockBattleEnemyHpCurrent;
        if (counterActionType == 1)
            counterRecordValueB = g_mockBattleRoleHpCurrent;
    }
    firstRecordValueB = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_B", firstRecordValueB);
    counterRecordValueB = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", counterRecordValueB);
    if (terminalFollowup)
    {
        if (!vm_net_mock_append_battle_status7_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, 4);
        vm_net_trace("mock_battle_operate_response_fallback response=4/7+4/8+4/11+4/9 terminalFollowup=1 result=1 case8_result=1 case8_autorevive=1 case11_result=1 case11_type=1 case9_result=1 roleHp=%u/%u enemyHp=%u/%u session=%u turn=%u len=%u evidence=runtime:latest_inline_4_7_4_11_4_9_passes_subtype9_gate_but_stays_on_Battle_auto_prompt,runtime:prior_4_7_4_8_reaches_case8_apply_call_05189DB4_and_sub_4B70_send25_call,runtime:prior_4_7_4_8_4_11_still_consumes_case11_and_writes_mainObj_0x474_1_at_pc_05189B66,Battle.cbm:sub_7BD0_subtype8_requires_mode2_before_apply_Battle_cbm_05189D16_05189DB4,Battle.cbm:sub_7BD0_subtype9_result1_gate_checks_mainObj_0x474_or_0x470_before_05189C04\n",
                     g_mockBattleRoleHpCurrent,
                     g_mockBattleRoleHpMax,
                     g_mockBattleEnemyHpCurrent,
                     g_mockBattleEnemyHpMax,
                     g_mockBattleOperateSessionSerial,
                     g_mockBattleOperateTurnCounter,
                     pos);
        return pos;
    }
    memset(actionInfo, 0, sizeof(actionInfo));
    {
        u8 mappedActorWireSlot = firstRecordActorWireSlot;
        u8 mappedTargetWireSlot = enemySlot;
        if (firstActionType == 1)
        {
            mappedActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_FIRST_ACTOR_WIRE_SLOT", 1);
            mappedTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_FIRST_TARGET_WIRE_SLOT", 0);
        }
        firstRecordWireActorUsed = mappedActorWireSlot;
        firstRecordWireTargetUsed = mappedTargetWireSlot;
        if (!terminalFollowup)
        {
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, firstActionType,
                                                             mappedActorWireSlot,
                                                             mappedTargetWireSlot,
                                                             firstRecordChildFlag, attackDamageValue,
                                                             firstRecordValueB,
                                                             firstActionType == 1 ? type1EffectIndex : 0,
                                                             firstActionType == 1 ? type1Tail0 : 0,
                                                             firstActionType == 1 ? type1Tail1 : 0,
                                                             firstActionType == 1 ? type1Tail2 : 0))
            {
                vm_net_trace("mock_battle_operate_response_fallback reject=actioninfo_build_failed cap=%u partialLen=%u index=%u operate=%u evidence=runtime:two_record_tagged_seq_payload_exceeded_or_failed_before_packet_assembly\n",
                             (unsigned int)sizeof(actionInfo),
                             actionInfoLen,
                             (unsigned int)index,
                             (unsigned int)operate);
                return 0;
            }
        }
        if (allowCounterattack)
        {
            u8 counterActorWireSlot = 2u;
            if (counterActionType == 1)
                counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT", 0);
            actionCount = 2;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, counterActionType,
                                                             counterActorWireSlot, 1u, 1u,
                                                             counterDamageValue, counterRecordValueB,
                                                             counterActionType == 1 ? type1EffectIndex : 0,
                                                             counterActionType == 1 ? type1Tail0 : 0,
                                                             counterActionType == 1 ? type1Tail1 : 0,
                                                             counterActionType == 1 ? type1Tail2 : 0))
            {
                vm_net_trace("mock_battle_operate_response_fallback reject=round_bundle_record1_build_failed cap=%u partialLen=%u index=%u operate=%u evidence=runtime:operate0_round_bundle_second_safe_record_could_not_be_encoded_in_fallback_builder\n",
                             (unsigned int)sizeof(actionInfo),
                             actionInfoLen,
                             (unsigned int)index,
                             (unsigned int)operate);
                return 0;
            }
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, 6, &objectStart) ||
        !vm_net_mock_put_battle_action_companion_fields(out, outCap, &pos) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "actionnum", actionCount) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "actioninfo", actionInfo, (u16)actionInfoLen))
    {
        vm_net_trace("mock_battle_operate_response_fallback reject=packet_build_failed outCap=%u pos=%u actionInfoLen=%u index=%u operate=%u evidence=runtime:4_2_header_only_fallback_still_needs_durable_failure_reason_before_raw82_tailpath\n",
                     outCap,
                     pos,
                     actionInfoLen,
                     (unsigned int)index,
                     (unsigned int)operate);
        return 0;
    }

    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    if (battleEndsThisRound)
    {
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 1;
    }
    vm_net_trace_bytes("mock_battle_operate_actioninfo_payload_fallback", actionInfo, actionInfoLen);
    vm_net_trace("mock_battle_operate_response_fallback response=%s actionnum=%u actioninfoLen=%u companionFields=omitted_until_teaminfo_record_grammar wireMapped=%u_to_%u index=%u operate=%u childFlag=%u damageValue=%u firstValueB=%u counterDamageValue=%u counterValueB=%u type1EffectIndex=%u type1Tail=%u,%u,%u roleHp=%u/%u enemyHp=%u/%u session=%u turn=%u bundleWholeRound=%u valueBRemainingHp=%u battleEnds=%u terminalFollowup=%u evidence=runtime:latest_type1_single_record_no_crash_reaches_damage_dispatch_but_enemy0_stays_20_20_with_valueB0,runtime:4_2_request_regressed_to_unhandled_packet_assert_so_dispatcher_now_forces_parser_safe_battle_reply_after_second_record_null_owner_crash_at_05188fd0,Battle.cbm:sub_6D12_reads_iteminfo_before_actioninfo,Battle.cbm:sub_6DBC_reads_teaminfo_before_actioninfo,Battle.cbm:sub_6DBC_present_teaminfo_consumes_current_team_record_count_not_zero_record_stream,Battle.cbm:sub_6EB0_materializes_type1_valueA_valueB_fields_05188f96_05188fbc,Battle.cbm:sub_4582_0x4582_type2_only_effect_while_type1_reads_damage_popup_fields,Battle.cbm:0x05189018_0x05189022_loops_over_actionnum_and_reenters_0x05188e30,Battle.cbm:sub_7BD0_subtype9_result1_gate_checks_mainObj_0x474_or_0x470_before_05189C04\n",
                  battleEndsThisRound ? "4/6(lethal-local-ko-pending)" : "4/6",
                  (unsigned int)actionCount,
                  actionInfoLen,
                 (unsigned int)firstRecordWireActorUsed,
                 (unsigned int)firstRecordWireTargetUsed,
                 (unsigned int)index,
                 (unsigned int)operate,
                 (unsigned int)firstRecordChildFlag,
                 attackDamageValue,
                 firstRecordValueB,
                 counterDamageValue,
                 counterRecordValueB,
                 type1EffectIndex,
                 (unsigned int)type1Tail0,
                 (unsigned int)type1Tail1,
                 (unsigned int)type1Tail2,
                 g_mockBattleRoleHpCurrent,
                 g_mockBattleRoleHpMax,
                 g_mockBattleEnemyHpCurrent,
                 g_mockBattleEnemyHpMax,
                 g_mockBattleOperateSessionSerial,
                 g_mockBattleOperateTurnCounter,
                 bundleWholeRound ? 1u : 0u,
                 type1ValueBRemainingHp ? 1u : 0u,
                 battleEndsThisRound ? 1u : 0u,
                 terminalFollowup ? 1u : 0u);
    return pos;
}

static u32 vm_net_mock_build_battle_operate_response_raw82(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    u32 len = vm_net_mock_build_battle_operate_response_fallback(request, requestLen, out, outCap);

    if (len != 0)
    {
        vm_net_trace("mock_battle_operate_response_raw82 len=%u mode=tagged_seq_forwarder evidence=runtime:raw82_tailpath_was_the_live_4_2_claimer_so_it_now_reuses_the_current_tagged_actioninfo_builder_instead_of_the_old_bare_43byte_payload\n",
                     len);
        return len;
    }

    vm_net_trace("mock_battle_operate_response_raw82 reject=fallback_builder_failed requestLen=%u evidence=runtime:tail_4_2_claimer_could_not_materialize_even_the_header_only_tagged_seq_reply\n",
                 requestLen);
    return 0;
}

static bool vm_net_mock_append_battle_status7_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u32 roleHp = g_mockBattleRoleHpMax != 0 ? g_mockBattleRoleHpCurrent : vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", 120);
    u32 roleMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MP", 40);
    const u32 statusLastExp = 1;
    const u32 statusCurExp = 1;
    const u16 statusPercentExp = 1;
    const u32 statusGold = 1;
    const u32 statusLevel = 2;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 7, &objectStart))
        return false;
    /*
     * Static evidence from Battle.cbm sub_743C()/sub_7228() shows the subtype-7
     * crash path only happens when the early status7 delta fields backing
     * [slot+0x08]/[slot+0x0C] stay non-positive. Use small positive candidates
     * here so the next manual run can test whether top-level status deltas alone
     * are enough to bypass the bad 0x7258 guard path.
     */
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", statusLastExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "curexp", statusCurExp))
        return false;
    if (!vm_net_mock_put_object_u16(out, outCap, pos, "persentexp", statusPercentExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "energy", 100))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "energymax", 100))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "gold", statusGold))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "level", statusLevel))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "bagstatus", 0))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "hp", roleHp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "mp", roleMp))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "itemnum", 0))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", NULL, 0))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "combatinfo", NULL, 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "autorevive", 0))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "info", ""))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "fbs", NULL, 0))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "fdata", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_terminal_subtype8_object(u8 *out, u32 outCap, u32 *pos)
{
    static const u8 terminalInfo[12] = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01,
    };
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 8, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "autorevive", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "info", terminalInfo, sizeof(terminalInfo)))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_terminal_case4_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 4, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_terminal_case9_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 9, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_terminal_case11_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 11, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 1))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_battle_auto12_ack_response(const u8 *request, u32 requestLen,
                                                        u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 kind = 0;
    u8 subtype = 0;

    if (outCap < pos ||
        !vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) ||
        kind != 4 || subtype != 12)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, 12, &objectStart))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_battle_auto12_ack_response response=4/12 empty len=%u evidence=runtime:after_terminal_4_7_4_11_4_9_client_sends_empty_4_12_before_assert,Battle.cbm:sub_7BD0_kind4_dispatch_cmp_subtype_0x0c_bhs_return_so_4_12_response_is_parser_safe_noop,Battle.cbm:outgoing_request_builder_05184A70_emits_kind4_subtype12_after_phase8\n",
                 pos);
    return pos;
}

static u32 vm_net_mock_build_challenge_interaction_response(const u8 *request, u32 requestLen,
                                                            u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    const u8 *moveInfo = NULL;
    u16 moveInfoLen = 0;
    bool hasMoveinfo = false;
    u8 battleInfo[160];
    u32 battleInfoLen = 0;
    u32 id = 0;
    u32 enemyWireId = 0;
    u32 enemyTable = 0;
    u32 enemyTableIds[4] = {0};
    u32 index = 0;
    u32 posx = 0;
    u32 posy = 0;
    bool prefillEnemyTemplate = false;
    u32 responseObjectCount = 1;

    if (outCap < pos || !vm_net_mock_is_challenge_interaction_request(request, requestLen))
        return 0;
    hasMoveinfo = vm_net_mock_get_object_blob_field(request, requestLen, "moveinfo", &moveInfo, &moveInfoLen);
    if (hasMoveinfo)
        (void)vm_net_mock_snapshot_current_player_pos("moveinfo-upload-combo");
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "id", &id);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "index", &index);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posx", &posx);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posy", &posy);

    enemyWireId = vm_net_mock_resolve_battle_enemy_id(id, &enemyTable, enemyTableIds);
    prefillEnemyTemplate = vm_net_mock_env_u8("CBE_BATTLE_PREFILL_ENEMY_TEMPLATE", 1) != 0 &&
                           id != 0 && id != 10001 && enemyWireId != id;
    if (prefillEnemyTemplate)
        enemyWireId = id;
    battleInfoLen = vm_net_mock_build_battle_start_info_blob(battleInfo, sizeof(battleInfo), 10001, enemyWireId);
    if (battleInfoLen == 0 || battleInfoLen > 0xffff)
        return 0;

    if (prefillEnemyTemplate)
    {
        if (!vm_net_mock_append_battle_enemy_template_prefill_object(out, outCap, &pos, id))
            return 0;
        ++responseObjectCount;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, 10, &objectStart))
        return 0;
    /*
     * Latest battle-operate evidence:
     * - subtype-6 valueA now drives the visible damage amount correctly
     * - but Battle.cbm sub_6CE8() still remaps wire slot ids when side==1
     * - current one-vs-one side==1 setup swaps 0 <-> 1, which matches the
     *   observed self-hit record=2,0,1 / targetId=1 path
     *
     * Keep battleinfo and operate record grammar unchanged and vary only side
     * to test whether target ownership follows the side-controlled slot mapper.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "side", 0))
        return 0;
    if (!vm_net_mock_put_object_raw(out, outCap, &pos, "battleinfo", battleInfo, (u16)battleInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (hasMoveinfo)
    {
        if (!vm_net_mock_append_actor_moveinfo_empty_ack_object(out, outCap, &pos))
            return 0;
        ++responseObjectCount;
    }
    vm_net_mock_finish_wt_packet(out, pos, responseObjectCount);
    g_mockBattleOperateSessionArmed = 1;
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattleOperateTurnCounter = 0;
    ++g_mockBattleOperateSessionSerial;
    g_mockBattleRoleHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", 120);
    g_mockBattleRoleHpMax = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_HP", g_mockBattleRoleHpCurrent);
    if (g_mockBattleRoleHpMax < g_mockBattleRoleHpCurrent)
        g_mockBattleRoleHpMax = g_mockBattleRoleHpCurrent;
    g_mockBattleEnemyHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", 20);
    g_mockBattleEnemyHpMax = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", g_mockBattleEnemyHpCurrent);
    if (g_mockBattleEnemyHpMax < g_mockBattleEnemyHpCurrent)
        g_mockBattleEnemyHpMax = g_mockBattleEnemyHpCurrent;
    vm_net_trace("mock_challenge_interaction_response response=%s side=0 battleinfoEncoding=raw-typed-stream battleinfoLen=%u requestEnemyId=%u enemyWireId=%u enemyTable=%08x enemyTableIds=%u,%u,%u,%u prefillEnemyTemplate=%u responseObjects=%u index=%u pos=%u,%u roleHp=%u/%u enemyHp=%u/%u status7=0 moveinfoAck=%u moveinfoLen=%u first=%02x,%02x,%02x,%02x len=%u evidence=Battle.cbm:sub_7BD0/sub_66CC,sub_66A4_enemy_lookup,IDA:net_handle_group_info_subtype5_0x0101208C_to_AddRoleToList_0x01012116 hypothesis=same_packet_5_5_prefill_can_populate_client_template_table_before_4_10 runtime_negative=%s\n",
                 prefillEnemyTemplate ? "5/5+4/10" : "4/10",
                 (unsigned int)battleInfoLen,
                 (unsigned int)id,
                 (unsigned int)enemyWireId,
                 enemyTable,
                 enemyTableIds[0],
                 enemyTableIds[1],
                 enemyTableIds[2],
                 enemyTableIds[3],
                 prefillEnemyTemplate ? 1u : 0u,
                 responseObjectCount,
                 (unsigned int)index,
                 (unsigned int)posx,
                 (unsigned int)posy,
                 g_mockBattleRoleHpCurrent,
                 g_mockBattleRoleHpMax,
                 g_mockBattleEnemyHpCurrent,
                 g_mockBattleEnemyHpMax,
                 hasMoveinfo ? 1u : 0u,
                 (unsigned int)moveInfoLen,
                 hasMoveinfo && moveInfoLen > 0 ? moveInfo[0] : 0,
                 hasMoveinfo && moveInfoLen > 1 ? moveInfo[1] : 0,
                 hasMoveinfo && moveInfoLen > 2 ? moveInfo[2] : 0,
                 hasMoveinfo && moveInfoLen > 3 ? moveInfo[3] : 0,
                 pos,
                 (enemyWireId == 10001 && id != 10001) ?
                     "enemy_template_fallback_is_player_character" :
                     (prefillEnemyTemplate ?
                          "prefill_template_experiment_pending_runtime_confirmation" :
                          "4_10_only_render_null_callback_pending"));
    vm_net_trace("mock_battle_operate_session arm=1 serial=%u turnCounter=%u roleHp=%u/%u enemyHp=%u/%u evidence=runtime:4_10_battle_start_response_is_the_nearest_server_boundary_before_first_4_2_operate_request\n",
                 g_mockBattleOperateSessionSerial,
                 g_mockBattleOperateTurnCounter,
                 g_mockBattleRoleHpCurrent,
                 g_mockBattleRoleHpMax,
                 g_mockBattleEnemyHpCurrent,
                 g_mockBattleEnemyHpMax);
    return pos;
}

static u32 vm_net_mock_build_special_scene_interaction_response(const u8 *request, u32 requestLen,
                                                                u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 id = 0;
    u32 index = 0;
    u32 posx = 0;
    u32 posy = 0;
    const char *scene = vm_net_mock_current_scene_name();
    char sceneForTrace[64];
    const char sourceScene01[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_01.sce */
    const char sourceScene03[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_03.sce */
    const char targetScene02[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_02.sce */
    const char targetScene04[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_04.sce */
    const char *targetScene = NULL;
    u16 targetX = 0;
    u16 targetY = 0;
    bool saveImmediately = false;

    if (outCap < pos || !vm_net_mock_is_challenge_interaction_request(request, requestLen))
        return 0;
    snprintf(sceneForTrace, sizeof(sceneForTrace), "%s", scene ? scene : "");
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "id", &id);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "index", &index);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posx", &posx);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posy", &posy);

    /*
     * Runtime evidence on 2026-06-13: id=105 is also used by monsters. A broad
     * id/index/posy gate steals battle challenges, so only answer the known
     * portal rectangles recovered from the same-class scene table.
     */
    if (id == 105 && index == 4 &&
        posx >= 208 && posx <= 256 && posy >= 432 && posy <= 448 &&
        strcmp(scene, sourceScene01) == 0)
    {
        targetScene = targetScene02;
        targetX = 80;
        targetY = 60;
        saveImmediately = true;
    }
    else if (id == 4 && index == 4 &&
             posx >= 160 && posx <= 240 && posy >= 553 && posy <= 570 &&
             strcmp(scene, sourceScene03) == 0)
    {
        targetScene = targetScene04;
        targetX = 42;
        targetY = 60;
    }
    else
    {
        return 0;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, false, false, 0, targetScene, targetX, targetY))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (saveImmediately)
        vm_net_mock_save_player_pos_state(targetScene, targetX, targetY, "special-scene-interaction");
    else
        vm_net_mock_mark_pending_scene_pos_save(targetScene, targetX, targetY, "special-scene-interaction");
    vm_net_trace("mock_special_scene_interaction_response request=4/1 id=%u index=%u pos=%u,%u scene=%s target=%s targetPos=%u,%u save=%s len=%u\n",
                 id,
                 index,
                 posx,
                 posy,
                 sceneForTrace,
                 targetScene,
                 (unsigned int)targetX,
                 (unsigned int)targetY,
                 saveImmediately ? "immediate" : "pending",
                 pos);
    return pos;
}

static u32 vm_net_mock_build_scene_interaction_followup_response(const u8 *request, u32 requestLen,
                                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;

    if (outCap < pos || !vm_net_mock_is_scene_interaction_followup_request(request, requestLen))
        return 0;
    /*
     * The live post-scene interaction request bundles 14/14,14/4,14/5,14/6,
     * but main business response dispatch has no kind-14 consumer. Answer only
     * the confirmed 2/10 branch so net_handle_actor_move_info() refreshes the
     * compact current-node tuple and the unknown 14-family remains evidence.
     */
    if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_scene_interaction_followup_response objects=1 othernum=0 ignoredReq14=14,4,5,6 len=%u\n",
                 pos);
    return pos;
}

static bool vm_net_mock_append_info_banner_clear12_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x19, 12, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 4))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_info_banner_result5_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x19, 5, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 4))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_resource_followup_objects(u8 *out, u32 outCap, u32 *pos,
                                                               u8 *objectCount, bool includeSkillBooks,
                                                               bool includeTaskLists,
                                                               bool includeActorOther, bool includeInfoBanner,
                                                               bool includeFbTargetClear,
                                                               bool includeFbTargetSeedOnly)
{
    /*
     * In the first scene-enter dispatch window we have a confirmed practical
     * limit of 10 objects.  Keep the skill/book pair plus empty 6/1 taskinfo
     * in this window; omitting either causes the client to immediately request
     * the large 12/1+7/42+6/*+2/10+25/5 follow-up after 30/1 closes the gate.
     */
    if (includeSkillBooks)
    {
        if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, pos))
            return false;
        *objectCount += 2;
    }

    if (!vm_net_mock_append_taskinfo_empty1_object(out, outCap, pos))
        return false;
    *objectCount += 1;

    /*
     * Case 13 loops over six tasktype slots. Each slot is a tagged i8 type id
     * followed by a len16 C string; use raw field payload so the stream cursor
     * starts directly at the first record.
     */
    if (includeTaskLists)
    {
        if (!vm_net_mock_append_tasktypes_empty13_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_taskaction_empty14_object(out, outCap, pos))
            return false;
        *objectCount += 1;
    }

    if (includeActorOther)
    {
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, pos))
            return false;
        *objectCount += 1;
    }

    if (includeInfoBanner)
    {
        if (!vm_net_mock_append_info_banner_result5_object(out, outCap, pos))
            return false;
        *objectCount += 1;
    }

    /*
     * Earlier experiments showed the 27/12+27/11 family suppresses the large
     * WT49 follow-up, while 27/11 alone does not set the needed fb state. Pair
     * 27/12 and 27/11 with 27/4 ready/finalize so the gate is restored in the
     * same follow-up scan.
     */
    if (includeFbTargetClear)
    {
        if (!vm_net_mock_append_fb_target_result12_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, pos, 1, ""))
            return false;
        *objectCount += 1;
    }
    else if (includeFbTargetSeedOnly)
    {
        /*
         * Hypothesis under test: after the requested WT49 resource/task/other
         * family is answered, run the complete fb-target trio in one follow-up
         * scan. 27/11 alone leaves sceneGateA at 0, but 27/4 immediately after
         * it restores the gate pair; use a non-empty info string so case 27/4
         * also exercises its scene-object info side effects.
         */
        if (!vm_net_mock_append_fb_target_result12_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, pos, 1,
                                                        vm_net_mock_fb_target_info_text()))
            return false;
        *objectCount += 1;
    }

    return true;
}

static u32 vm_net_mock_build_scene_resource_followup_response(const u8 *request, u32 requestLen,
                                                              u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    bool includeSkillBooks = false;
    bool keepBusinessGate = false;
    if (outCap < pos || !vm_net_mock_is_scene_resource_followup_request(request, requestLen))
        return 0;

    includeSkillBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
                        vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    keepBusinessGate = includeSkillBooks &&
                       vm_net_mock_env_u8("CBE_FIRST_SCENE_KEEP_BUSINESS_GATE", 1) != 0;
    if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                           includeSkillBooks, true, true,
                                                           includeSkillBooks ? false : true, false,
                                                           includeSkillBooks && !keepBusinessGate))
        return 0;
    if (keepBusinessGate)
    {
        u32 objectStart = 0;
        /*
         * Probe the first-scene contract without forcing client globals:
         * 27/12 and full 30/1 both close sceneObj+0x164 before later local
         * HUD responses. A 30/2 result/scene ack without posinfo exercises the
         * scene completion follow-up path but avoids that close branch.
         */
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, vm_net_mock_scene_key_name()))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
    }
    else if (!vm_net_mock_append_scene_enter_object(out, outCap, &pos))
    {
        return 0;
    }
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_scene_resource_followup_response objects=%u skillBooks=%u taskinfo=empty tasktypes=6xempty task14=zero othernum=0 result25_5=%u fbFull12_11_4Info=%u trailingSceneEnter=%u keepBusinessGate=%u len=%u\n",
                 objectCount, includeSkillBooks ? 1u : 0u, includeSkillBooks ? 0u : 1u,
                 (includeSkillBooks && !keepBusinessGate) ? 1u : 0u,
                 keepBusinessGate ? 0u : 1u,
                 keepBusinessGate ? 1u : 0u,
                 pos);
    return pos;
}

static u32 vm_net_mock_build_scene_task_subset_followup_response(const u8 *request, u32 requestLen,
                                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    bool completeDeferredScene = g_vm_net_mock_last_scene_change_target_valid;
    if (outCap < pos || !vm_net_mock_is_scene_task_subset_followup_request(request, requestLen))
        return 0;

    /*
     * This post-scene-change request is the WT49 task/other/banner subset
     * without skill/book objects. Experiments that moved scene completion into
     * this response caused either bounce-back or late gate-off, so keep this
     * response parser-safe and side-family only.
     */
    if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                           false, true, true, true, false, false))
        return 0;
    if (completeDeferredScene)
    {
        const vm_net_mock_scene_change_target *target = &g_vm_net_mock_last_scene_change_target;
        u32 objectStart = 0;
        if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos, target->scene, target->x, target->y))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                        g_vm_net_mock_last_scene_change_fb4_type,
                                                        vm_net_mock_fb_target_info_text()))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2,
                                               target->scene, target->x, target->y))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        vm_net_mock_mark_completed_scene_change_target(target);
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_scene_task_subset_followup_response objects=%u taskinfo=empty tasktypes=6xempty task14=zero othernum=0 result25_5=4 trailingSceneEnter=0 deferredSceneChangeResult=%u deferredSceneCompletion=%u lateDispatchExpected=%u len=%u\n",
                 objectCount,
                 completeDeferredScene ? 1u : 0u,
                 completeDeferredScene ? 1u : 0u,
                 completeDeferredScene ? 0u : 1u,
                 pos);
    return pos;
}

static u32 vm_net_mock_build_short_wt_control_echo_response(const u8 *request, u32 requestLen,
                                                            u8 kind, u8 subtype,
                                                            u8 *out, u32 outCap)
{
    /*
     * 0x63/1 sits on the startup/login bridge. Dropping it leaves the progress
     * spinner running forever; while the exact protocol is still unknown, the
     * client does continue when it receives a short control ack back.
     */
    vm_net_trace("mock_short_wt_control_echo kind=%u subtype=%u len=%u\n",
                 kind, subtype, requestLen);
    return vm_net_mock_copy_response(request, requestLen, out, outCap);
}

static u32 vm_net_mock_build_practise_info18_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 18, &objectStart))
        return 0;
    /*
     * sub_102CB46 handles the active practise-info panel. For subtype 18 it
     * reads todaypasthour, todaypastmin, getexp, todaylasthour,
     * todaylastmin, alllasthour, alllastmin, then isgold.
     */
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaypasthour", 0))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaypastmin", 15))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "getexp", 120))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaylasthour", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaylastmin", 45))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "alllasthour", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "alllastmin", 45))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "isgold", 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_practise_info18_response response=7/18 today=0:15 getexp=120 last=1:45 all=1:45 isgold=0 len=%u\n", pos);
    return pos;
}

static bool vm_net_mock_is_short_wt_control_packet(const u8 *request, u32 requestLen, u8 kind, u8 subtype);

static u32 vm_net_mock_build_battle_death_prompt_followup_response(const u8 *request, u32 requestLen,
                                                                   u8 *out, u32 outCap)
{
    /*
     * Latest confirmed runtime:
     * - subtype-6 attack now reaches Battle.cbm action parser at tick 216.
     * - the client then shows the prompt "您已经死亡，是否进入商城购买复活石?" via
     *   sub_103838A() result==2 -> sub_10110E6() -> ui_show_message_box().
     * - after the user clicks the prompt, the client emits a short one-object
     *   request `WT len=21 hdr=7/14 objs=1/7/14(result=2)`.
     *
     * The real server-side business contract after this click is still unknown.
     * For now, keep the transport/session alive with a same-packet echo rather
     * than guessing a shop / revive semantic family.
     */
    if (!vm_net_mock_is_short_wt_control_packet(request, requestLen, 7, 14))
        return 0;
    vm_net_trace("mock_battle_death_prompt_followup_response response=echo kind=7 subtype=14 len=%u evidence=runtime_tick216_message_box_text_dead_buy_revive_stone_and_post_click_request_WT_len21_hdr7_14 hypothesis=real_shop_followup_contract_still_unknown\n",
                 requestLen);
    return vm_net_mock_copy_response(request, requestLen, out, outCap);
}

static u32 vm_net_mock_build_shop_actor_query14_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 actorId = 0;
    vm_net_mock_request_object object;

    /*
     * Current post-death flow now reaches a second-stage one-object request:
     *   WT len=25 hdr=1/14 objs=1/1/14(actorId=0)
     * Latest static/raw evidence also shows the camel-case field name
     * `actorId` inside mmShopMstarWqvga.cbm rather than the main-CBE
     * `actorID` title/login family, so treat this as a shop-side follow-up.
     *
     * The real 1/14 shop/revive payload contract is still unknown. Keep the
     * transport alive with a same-packet echo until the response consumer is
     * recovered.
     */
    if (request == NULL || requestLen < 9 || out == NULL)
        return 0;
    if (request[0] != 'W' || request[1] != 'T')
        return 0;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return 0;
    if (offset != requestLen)
        return 0;
    if (object.major != 1 || object.kind != 1 || object.subtype != 14)
        return 0;
    if (!vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "actorId", &actorId))
        return 0;

    vm_net_trace("mock_shop_actor_query14_response response=echo kind=1 subtype=14 actorId=%u len=%u evidence=runtime_post_death_prompt_second_stage_request_WT_len25_hdr1_14_and_mmShopMstarWqvga_cbm_actorId_string hypothesis=real_shop_followup_contract_still_unknown\n",
                 actorId,
                 requestLen);
    return vm_net_mock_copy_response(request, requestLen, out, outCap);
}

static bool vm_net_mock_is_login_tail_skill_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen != 14)
        return false;
    if (request[0] != 'W' || request[1] != 'T')
        return false;

    u32 offset = 4;
    vm_net_mock_request_object object;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x0c || object.subtype != 1 || object.payloadLen != 0)
        return false;

    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 42 || object.payloadLen != 0)
        return false;

    return offset == requestLen;
}

static u32 vm_net_mock_build_login_tail_skill_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_net_trace("mock_login_tail_skill_response learnednum=0 learnedskill=0 booknum=0 booksinfo=0 len=%u\n", pos);
    return pos;
}

static bool vm_net_mock_is_short_wt_control_packet(const u8 *request, u32 requestLen, u8 kind, u8 subtype)
{
    if (request == NULL || requestLen != 9)
        return false;
    return request[0] == 'W' &&
           request[1] == 'T' &&
           request[2] == 0 &&
           request[3] == 9 &&
           request[4] == 1 &&
           request[5] == kind &&
           request[6] == subtype &&
           request[7] == 0 &&
           request[8] == 5;
}

static const char *vm_net_mock_actor_resource_name(u8 actorJob, u8 actorSex)
{
    static const char *actorNames[6] = {
        "h_warrior.actor",
        "hW_warrior.actor",
        "h_assassin.actor",
        "hW_assassin.actor",
        "h_mage.actor",
        "hW_mage.actor",
    };
    u32 tableIndex = vm_net_mock_actor_resource_index(actorJob, actorSex);
    if (tableIndex >= sizeof(actorNames) / sizeof(actorNames[0]))
        tableIndex = 0;
    return actorNames[tableIndex];
}

static const char *vm_net_mock_actor_preview_image_name(u8 actorJob, u8 actorSex)
{
    const char *overrideName = vm_net_mock_env_str("CBE_ACTOR_PREVIEW_IMAGE", "");
    (void)actorJob;
    (void)actorSex;
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    return "";
}

static u32 vm_net_mock_build_actor_info(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char roleName[] = "\xcf\xc0\xbd\xa3\xbd\xad\xba\xfe"; /* GBK: xia jian jiang hu */
    const char *displayName = "JHOnline";
    u32 roleLevel = vm_net_mock_env_u8("CBE_ROLE_LEVEL", 1);
    u32 actorJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u32 actorSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);
    u32 visualVariant = 0;
    u32 visualGroup = 0;
    u32 primaryCurrent = vm_net_mock_env_u32("CBE_ACTOR_HP_CURRENT",
                                             vm_net_mock_env_u32("CBE_ACTOR_HP", 120));
    u32 primaryBaseMax = vm_net_mock_env_u32("CBE_ACTOR_HP_MAX", 120);
    u32 secondaryCurrent = vm_net_mock_env_u32("CBE_ACTOR_MP_CURRENT",
                                               vm_net_mock_env_u32("CBE_ACTOR_MP", 100));
    u32 secondaryBaseMax = vm_net_mock_env_u32("CBE_ACTOR_MP_MAX", 100);
    u32 actorSummaryValue = vm_net_mock_env_u32("CBE_ACTOR_SUMMARY_VALUE", 0);
    u32 actorGap09C0 = vm_net_mock_env_u32("CBE_ACTOR_GAP09C0", 0);
    u32 actorSummaryStatus = vm_net_mock_env_u32("CBE_ACTOR_STATUS_WORD", roleLevel);
    u8 actorStateByte0 = vm_net_mock_env_u8("CBE_ACTOR_STATE_BYTE0", 0);
    u8 actorStateByte1 = vm_net_mock_env_u8("CBE_ACTOR_STATE_BYTE1", 0);
    u8 actorTargetX = 12;
    u8 actorTargetY = 10;
    u8 actorField11E = 0;
    u8 actorField120 = 0;
    const char *shortLabel = vm_net_mock_env_str("CBE_ACTOR_SHORT_LABEL", "10001");
    const char *actorResource = NULL;
    const char *sceneKey = NULL;
    u16 actorResourceArg = 0;
    u16 actorGridX = 0;
    u16 actorGridY = 0;
    u16 motionResourceArg0 = 0;
    u16 motionResourceArg1 = 0;
    u32 primaryDisplayMax = vm_net_mock_env_u32("CBE_ACTOR_HP_DISPLAY_MAX", primaryBaseMax);
    u32 secondaryDisplayMax = vm_net_mock_env_u32("CBE_ACTOR_MP_DISPLAY_MAX", secondaryBaseMax);
    u32 actorGap0CC0 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC0", 0);
    u32 actorGap0CC4 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC4", 0);
    u32 actorGap0CC8 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC8", 0);

    if (roleLevel == 0)
        roleLevel = 1;
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    if (actorSex > 1)
        actorSex = 0;
    if (primaryBaseMax == 0)
        primaryBaseMax = 120;
    if (secondaryBaseMax == 0)
        secondaryBaseMax = 100;
    if (primaryCurrent > primaryBaseMax)
        primaryCurrent = primaryBaseMax;
    if (secondaryCurrent > secondaryBaseMax)
        secondaryCurrent = secondaryBaseMax;
    if (primaryDisplayMax == 0)
        primaryDisplayMax = primaryBaseMax;
    if (secondaryDisplayMax == 0)
        secondaryDisplayMax = secondaryBaseMax;

    /*
     * Fresh scene-enter keeps the compact title-facing knobs but the scene-side
     * portrait/widget picker is not fully 0-based. Static `scene_runtime_init_
     * and_sync()` now proves the portrait index is `visualGroup + 2*visualVariant - 1`,
     * so `visualVariant` remains 0-based (`job-1`) while `visualGroup` must stay
     * in the original `1..2` family rather than `0..1`.
     */
    visualVariant = actorJob - 1;
    visualGroup = actorSex + 1;

    actorTargetX = (u8)vm_net_mock_env_u32("CBE_ACTOR_TARGET_X", 12);
    actorTargetY = (u8)vm_net_mock_env_u32("CBE_ACTOR_TARGET_Y", 10);
    actorGridX = (u16)vm_net_mock_env_u32("CBE_ACTOR_GRID_X", vm_net_mock_scene_spawn_x());
    actorGridY = (u16)vm_net_mock_env_u32("CBE_ACTOR_GRID_Y", vm_net_mock_scene_spawn_y());
    actorField11E = vm_net_mock_env_u8("CBE_ACTOR_BYTE_11E", actorTargetX);
    actorField120 = vm_net_mock_env_u8("CBE_ACTOR_BYTE_120", actorTargetY);
    actorResource = vm_net_mock_actor_resource_name((u8)actorJob, (u8)actorSex);
    sceneKey = vm_net_mock_scene_key_name();
    actorResourceArg = (u16)vm_net_mock_env_u32("CBE_ACTOR_RESOURCE_ARG", 0);
    motionResourceArg0 = (u16)vm_net_mock_env_u32("CBE_ACTOR_MOTION_ARG0", actorGridX);
    motionResourceArg1 = (u16)vm_net_mock_env_u32("CBE_ACTOR_MOTION_ARG1", actorGridY);

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 10001))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)visualVariant))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)visualGroup))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 10001))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, displayName))
        return 0;

    /*
     * Fresh scene-enter (`parse_actorinfo_response(a2==0)`) does not match the
     * older guessed "level/current/base/..." order directly. The confirmed
     * stream from the second string onward is:
     *   tagged u32 summaryStatus (stored into a word slot)
     *   u32 primaryCurrent
     *   u32 primaryBaseMax
     *   u32 secondaryCurrent
     *   u32 secondaryBaseMax
     *   u32 extra132
     *   six truncated u32 -> word fields
     *   u32 summary176
     *   u32 gap09C0
     *   u8 state0
     *   u8 state1
     *   u32 primaryDisplayMax
     *   u32 secondaryDisplayMax
     *   u8 targetPosX
     *   u8 targetPosY
     *   str shortLabel
     *   str previewImage
     *   u32 gap0CC0
     *   u32 gap0CC4
     *   u32 gap0CC8
     *   str actorResource
     *   i16 actorResourceArg
     *   str sceneKey
     *   i16 motionArg0
     *   i16 motionArg1
     */
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorSummaryStatus))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryCurrent))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryBaseMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryCurrent))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryBaseMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, vm_net_mock_env_u32("CBE_ACTOR_EXTRA132", 0)))
        return 0;

    for (u32 i = 0; i < 6; ++i)
    {
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 0))
            return 0;
    }

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorSummaryValue))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap09C0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorStateByte0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorStateByte1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryDisplayMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryDisplayMax))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorField11E))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorField120))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, shortLabel))
        return 0;
    /*
     * This 20-byte mid-body string lands at actor+0x10A.  The final scene
     * consumer seen so far is `scene_draw_node_overhead_overlay()`, which treats
     * a non-empty value as an optional named overhead badge/icon. Do not seed it
     * with the actor's walk GIF; that makes body art appear as stray fragments.
     */
    if (!vm_net_mock_seq_put_string(out, outCap, &pos,
                                    vm_net_mock_actor_preview_image_name((u8)actorJob, (u8)actorSex)))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC0))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC4))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC8))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, actorResource))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, actorResourceArg))
        return 0;
    /*
     * parse_actorinfo_response copies this second trailing string directly into
     * R9+0x5E46, and fresh scene-enter later reuses that buffer as
     * currentMapIdText / the persistent +1141 scene-text slot. Keep it separate
     * from actorResource and the trailing grid/pose-like word pair.
     */
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, sceneKey))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, motionResourceArg0))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, motionResourceArg1))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_role_list_info(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char roleName[] = "\xcf\xc0\xbd\xa3\xbd\xad\xba\xfe"; /* GBK: xia jian jiang hu */
    u32 roleId = 10001;
    u32 roleLevel = vm_net_mock_env_u8("CBE_ROLE_LEVEL", 1);

    if (roleLevel == 0)
        roleLevel = 1;

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleLevel))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_title_role_list_actorinfo(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char roleName[] = "\xcf\xc0\xbd\xa3\xbd\xad\xba\xfe"; /* GBK: xia jian jiang hu */
    u32 roleId = 10001;
    u16 roleLevel = (u16)vm_net_mock_env_u8("CBE_ROLE_LEVEL", 1);
    u8 actorJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u8 actorSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);

    if (roleLevel == 0)
        roleLevel = 1;
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    /*
     * title target50_render() derives a role portrait table index from the
     * compact role-list record as `job * 2 + (sex - 1)`, so the title-side
     * role-list contract expects `sex` to be 1-based here. A zero value wraps
     * into 0xFF and drives the render path into an invalid callback slot.
     */
    if (actorSex < 1 || actorSex > 2)
        actorSex = 1;

    /*
     * mmTitleMstarWqvga.cbm sub_3544() parses its actorinfo field as:
     *   count, then repeated (tagged u32, tagged u8, tagged u8, len16+cstr, tagged i16).
     * The field name collides with later in-scene actorinfo, but the title-side
     * payload is a compact role-list entry table instead of the large player blob.
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorJob))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorSex))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, roleLevel))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_login_serverinfo_blob(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char serverName[] = "\xb2\xe2\xca\xd4\xd2\xbb\xc7\xf8"; /* GBK: ce shi yi qu */
    const char serverLabel[] = "\xcd\xc6\xbc\xf6"; /* GBK: tui jian */

    if (!vm_net_mock_seq_put_string(out, outCap, &pos, serverName))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, serverLabel))
        return 0;
    /* Keep the runtime-stable serverinfo shape: the title parser consumes the
     * first server attribute as a 32-bit field, but the trailing display color
     * is a 24-bit value. Using a visible color here avoids an "empty" looking
     * list when the server row is present but rendered with black-on-black. */
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u24(out, outCap, &pos, 0x00FFFFFF))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_title_servconf_blob(u8 *out, u32 outCap)
{
    u32 pos = 0;

    /*
     * mmTitleMstarWqvga.cbm sub_3544() reads servconf through sub_1490(),
     * which consumes a 5-byte tagged blob and copies bytes 2..4 into the
     * server-selection state. Reuse the same compact color-shape encoding here.
     */
    if (!vm_net_mock_seq_put_u24(out, outCap, &pos, 0x00FFFFFF))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_login_color_blob(u8 *out, u32 outCap)
{
    u32 pos = 0;

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u24(out, outCap, &pos, 0x00FFFFFF))
        return 0;

    return pos;
}

static u8 vm_net_mock_resolve_login_success_subtype(u8 requestSubtype)
{
    const char *rawSubtype = vm_net_mock_env_str("CBE_LOGIN_TOP_TYPE", "");
    u8 actorSubtype = requestSubtype;

    if (rawSubtype != NULL && rawSubtype[0] != 0)
        actorSubtype = vm_net_mock_env_u8("CBE_LOGIN_TOP_TYPE", requestSubtype);

    if (actorSubtype != 1 && actorSubtype != 2 && actorSubtype != 3 && actorSubtype != 6)
        actorSubtype = 6;

    return actorSubtype;
}

static bool vm_net_mock_append_login_success_object(u8 *out,
                                                    u32 outCap,
                                                    u32 *pos,
                                                    u8 actorSubtype,
                                                    bool includeActorInfo,
                                                    u32 *actorInfoLenOut)
{
    u8 actorInfo[512];
    u32 actorInfoLen = 0;

    if (includeActorInfo)
    {
        memset(actorInfo, 0, sizeof(actorInfo));
        actorInfoLen = vm_net_mock_build_actor_info(actorInfo, sizeof(actorInfo));
        if (actorInfoLen == 0)
            return false;
    }

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 1, actorSubtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "revivetype", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "ruffianflag", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 0))
        return false;
    if (actorSubtype == 6)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "practiseflag", 0))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "pcimg", 0))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard", 0))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expbook", 0))
            return false;
        if (!vm_net_mock_put_object_string(out, outCap, pos, "practiseinfo", ""))
            return false;
    }
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", 0))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "curexp", 0))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "persentexp", 0))
        return false;
    if (includeActorInfo)
    {
        if (!vm_net_mock_put_object_entry(out, outCap, pos, "actorinfo", actorInfo, (u16)actorInfoLen))
            return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (actorInfoLenOut)
        *actorInfoLenOut = actorInfoLen;
    return true;
}

static bool vm_net_mock_append_actorinfo_object(u8 *out, u32 outCap, u32 *pos, u32 *actorInfoLenOut)
{
    return vm_net_mock_append_login_success_object(out, outCap, pos, 1, true, actorInfoLenOut);
}

static u32 vm_net_mock_build_login_primary_validation_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 serverInfo[128];
    u32 serverInfoLen = 0;
    u8 colorBlob[16];
    u32 colorBlobLen = 0;

    if (outCap < pos)
        return 0;

    memset(serverInfo, 0, sizeof(serverInfo));
    memset(colorBlob, 0, sizeof(colorBlob));
    serverInfoLen = vm_net_mock_build_login_serverinfo_blob(serverInfo, sizeof(serverInfo));
    if (serverInfoLen == 0)
        return 0;
    colorBlobLen = vm_net_mock_build_login_color_blob(colorBlob, sizeof(colorBlob));
    if (colorBlobLen == 0)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "serverinfo", serverInfo, (u16)serverInfoLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "color", colorBlob, (u16)colorBlobLen))
        return 0;
    /*
     * IDA: mmTitle net_handle_login_response(0x16DC) reads "servernum"
     * through accessor slot [18], while "result"/"newVer" use [19].
     * Runtime disproved both the original u8 encoding and a later u32 trial:
     * the client still left serverCount==0 while result/newVer parsed. Use a
     * u16 field here as the next narrow contract probe for accessor[18].
     */
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "servernum", 1))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "newVer", 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (!g_netMockTitleServerSelectConfirmed)
        vm_net_mock_title_login_phase_mark_server_list();
    vm_net_trace("mock_login_primary_validation_response top=1,1,1 result=1 serverinfo_len=%u color_len=%u servernum_u16=1 len=%u phase=%u/%u evidence=runtime:direct_1_1_can_enter_title_list_screen_but_1_16_is_gated_until_1_4,trace_title_login_state_serverResult49_serverCount0_after_u8_and_u32 IDA:mmTitle_net_handle_login_response_0x16DC_reads_servernum_via_accessor18_and_result_newVer_via_accessor19,IDA:mmTitle_login_response_parse_server_color_table_0x14F4_optional_color_blob\n",
                 serverInfoLen,
                 colorBlobLen,
                 pos,
                 (u32)g_netMockTitleServerListPending,
                 (u32)g_netMockTitleServerSelectConfirmed);
    return pos;
}

static u32 vm_net_mock_build_login_failure_response(u8 requestSubtype, const char *information, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 responseSubtype = requestSubtype ? requestSubtype : 1;
    const char *message = information ? information : "password error";

    if (outCap < pos)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, responseSubtype, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 2))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "information", message))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_login_failure_response top=1,1,%u result=2 info=%s len=%u\n",
                 responseSubtype, message, pos);
    return pos;
}

static u32 vm_net_mock_build_login_primary_wait_server_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 serverInfo[128];
    u32 serverInfoLen = 0;
    u8 colorBlob[16];
    u32 colorBlobLen = 0;

    if (outCap < pos)
        return 0;

    memset(serverInfo, 0, sizeof(serverInfo));
    memset(colorBlob, 0, sizeof(colorBlob));
    serverInfoLen = vm_net_mock_build_login_serverinfo_blob(serverInfo, sizeof(serverInfo));
    if (serverInfoLen == 0)
        return 0;
    colorBlobLen = vm_net_mock_build_login_color_blob(colorBlob, sizeof(colorBlob));
    if (colorBlobLen == 0)
        return 0;

    /*
     * The primary login parser only accepts result '1' as the success path
     * that continues on to parse serverinfo/servernum/newVer. Keep this reply
     * narrow: server list metadata only, no actorinfo/role payload.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "serverinfo", serverInfo, (u16)serverInfoLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "color", colorBlob, (u16)colorBlobLen))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "servernum", 1))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "newVer", 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_title_login_phase_mark_server_list();
    vm_net_trace("mock_login_primary_wait_server_response top=1,1,1 result=1 serverinfo_len=%u color_len=%u servernum_u16=1 len=%u evidence=IDA:mmTitle_net_handle_login_response_0x16DC_primary_path_reads_serverinfo_servernum_newVer,IDA:mmTitle_login_response_parse_server_color_table_0x14F4_optional_color_blob,IDA:mmTitle_net_handle_login_response_0x16DC_reads_servernum_via_accessor18 runtime:serverlist_hold_must_use_result1_or_serverinfo_is_skipped,trace_title_login_state_serverResult49_serverCount0_after_u8_and_u32\n",
                 serverInfoLen,
                 colorBlobLen,
                 pos);
    return pos;
}

static bool vm_net_mock_append_role_list_object(u8 *out, u32 outCap, u32 *pos)
{
    u8 roleInfo[128];
    memset(roleInfo, 0, sizeof(roleInfo));
    u32 roleInfoLen = vm_net_mock_build_role_list_info(roleInfo, sizeof(roleInfo));
    if (roleInfoLen == 0)
        return false;

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0a, 0x20, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "roles", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "maxroles", 3))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "allpgs", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "num", 1))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "roleinfo", roleInfo, (u16)roleInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_title_mode15_result_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    /*
     * The active R9+0x29E8 title-local state machine has a narrower confirmed
     * gate: its type=1/subtype=15 success path goes straight to sub_39A4(),
     * which is the same mode<-1 transition we already correlated with the
     * role-selection state.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 1, 15, &objectStart))
        return false;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, pos, "result", 1))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_role_list_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_role_list_response roles=1 maxroles=3 num=1 allpgs=1 len=%u\n", pos);
    return pos;
}

static u32 vm_net_mock_build_title_mode15_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_title_mode15_result_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_title_mode15_response result=1 len=%u\n", pos);
    return pos;
}

static u32 vm_net_mock_build_title_server_select_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 serverId = 0;
    u32 moneyType = 0;
    u8 servConf[8];
    u32 servConfLen = 0;
    u8 actorInfo[128];
    u32 actorInfoLen = 0;

    if (outCap < pos)
        return 0;

    (void)vm_net_mock_get_object_u32_field(request, requestLen, "serverID", &serverId);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "moneytype", &moneyType);
    memset(servConf, 0, sizeof(servConf));
    servConfLen = vm_net_mock_build_title_servconf_blob(servConf, sizeof(servConf));
    if (servConfLen == 0)
        return 0;
    actorInfoLen = vm_net_mock_build_title_role_list_actorinfo(actorInfo, sizeof(actorInfo));
    if (actorInfoLen == 0)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 4, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "servconf", servConf, (u16)servConfLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "actorinfo", actorInfo, (u16)actorInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_title_login_phase_mark_server_select(serverId);
    vm_net_trace("mock_title_server_select_response top=1,1,4 result=1 servconf_len=%u actorinfo_len=%u serverID=%u moneytype=%u len=%u evidence=IDA:mmTitle_sub_3544_subtype4_reads_servconf_before_role_table,IDA:mmTitle_sub_3544_subtype4_role_table_payload_later runtime:server_select_request_fields_serverID_moneytype\n",
                 servConfLen,
                 actorInfoLen,
                 serverId,
                 moneyType,
                 pos);
    return pos;
}

static u32 vm_net_mock_build_title_rolelist_stage_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 actorinfo[128];
    u32 actorinfoLen = 0;

    if (outCap < pos)
        return 0;

    actorinfoLen = vm_net_mock_build_title_role_list_actorinfo(actorinfo, sizeof(actorinfo));
    if (actorinfoLen == 0)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 16, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 4, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "actorinfo", actorinfo, (u16)actorinfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_net_trace("mock_title_rolelist_stage_response top=1,1,16+1,1,4 actorinfo_len=%u len=%u evidence=runtime:client_sends_explicit_1_16_after_login_ack IDA:mmTitle_title_handle_role_list_response_0x3544_reads_16_result_then_4_actorinfo\n",
                 actorinfoLen, pos);
    return pos;
}

static u32 vm_net_mock_build_title_rolelist_wait_server_select_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (outCap < pos)
        return 0;

    /*
     * role_list_screen_render() can emit the 1/1/16 role-list request as soon
     * as primary login success moves into the title list screen. Until the
     * client has sent the explicit 1/1/4 serverID/moneytype confirmation, keep
     * this as a parser-safe no-op reply. The actual actorinfo object is only
     * valid after server selection, and subtype 16 result '1' is the cached
     * success latch that the title parser expects before it accepts subtype 4
     * actorinfo. Keep this reply minimal so it only advances the state machine
     * and does not mix the later role payload into the same packet.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 16, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_title_rolelist_wait_server_select_response top=1,1,16 result=1 len=%u phase=%u/%u evidence=IDA:mmTitle_sub_3544_subtype16_result1_caches_success_before_subtype4_actorinfo runtime:1_16_is_parser_safe_only_when_it_does_not_carry_actorinfo\n",
                 pos,
                 (u32)g_netMockTitleServerListPending,
                 (u32)g_netMockTitleServerSelectConfirmed);
    return pos;
}

static u32 vm_net_mock_build_title_role_select_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 actorId = 10001;
    u32 actorInfoLen = 0;

    if (outCap < pos)
        return 0;

    (void)vm_net_mock_get_object_u32_field(request, requestLen, "actorID", &actorId);

    /* A minimal {result, actorID} subtype-6 ack is accepted by
     * role_manage_screen_handle_network(case 6), but as soon as subtype-15
     * pushes the client into the next bootstrap path the main scene-side logic
     * also sees this subtype-6 object. Static scene_runtime_init_and_sync()
     * treats subtype 6 as a full scene-enter family packet and reads fields
     * such as revivetype/type/practiseflag/pcimg/.../actorinfo from it. Reuse
     * the existing full subtype-6 success shell here so the follow-up subtype-15
     * experiment is not blocked by a missing scene-enter payload contract.
     */
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, 6, true, &actorInfoLen))
        return 0;

    if (!vm_net_mock_append_title_mode15_result_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_net_trace("mock_title_role_select_response top=1,1,6(full)+1,1,15 actorID=%u actorinfo_len=%u len=%u\n",
                 actorId,
                 actorInfoLen,
                 pos);
    return pos;
}

static u32 vm_net_mock_build_login_alt12_server_list_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 serverInfo[128];
    u32 serverInfoLen = 0;
    u8 colorBlob[16];
    u32 colorBlobLen = 0;
    char userName[64];
    char password[64];
    bool haveUserName = false;
    bool havePassword = false;
    u8 resultCode = vm_net_mock_env_u8("CBE_ALT12_SERVERLIST_RESULT", 4);
    bool echoCredentials = vm_net_mock_env_u8("CBE_ALT12_ECHO_CREDENTIALS", 0) != 0;

    if (outCap < pos)
        return 0;

    memset(serverInfo, 0, sizeof(serverInfo));
    memset(colorBlob, 0, sizeof(colorBlob));
    memset(userName, 0, sizeof(userName));
    memset(password, 0, sizeof(password));
    serverInfoLen = vm_net_mock_build_login_serverinfo_blob(serverInfo, sizeof(serverInfo));
    if (serverInfoLen == 0)
        return 0;
    colorBlobLen = vm_net_mock_build_login_color_blob(colorBlob, sizeof(colorBlob));
    if (colorBlobLen == 0)
        return 0;

    haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "username", userName, sizeof(userName));
    if (!haveUserName)
        haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "userName", userName, sizeof(userName));
    havePassword = vm_net_mock_get_object_string_field(request, requestLen, "password", password, sizeof(password));

    if (resultCode != 3 && resultCode != 4)
        resultCode = 4;

    /*
     * mmTitle net_handle_login_response(0x16DC) treats subtype-12 result '1'
     * as a staged success and login_alt_result_dispatch(0x19C2) immediately
     * enters the stageFlag==4 role-list target. Result '3'/'4' on the subtype
     * 12 path is the parser branch that still consumes serverinfo/servernum.
     * Runtime showed result '3' can loop back through the sub_5916 success
     * callback path; prefer result '4', whose sub_5922 callback checks the
     * title confirmation byte before calling login_stage_success_dispatch().
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 12, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", resultCode))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "serverinfo", serverInfo, (u16)serverInfoLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "color", colorBlob, (u16)colorBlobLen))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "servernum", 1))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "newVer", 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "information", "select server"))
        return 0;
    if (echoCredentials)
    {
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "username", haveUserName ? userName : ""))
            return 0;
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "password", havePassword ? password : ""))
            return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_title_login_phase_mark_server_list();
    vm_net_trace("mock_login_alt12_server_list_response top=1,1,12 result=%u serverinfo_len=%u color_len=%u servernum_u16=1 credentialEcho=%u user=%s len=%u evidence=IDA:mmTitle_net_handle_login_response_0x16DC_result3_or_4_alt_path_parses_serverinfo_and_later_reads_credential_fields,IDA:mmTitle_login_response_parse_server_color_table_0x14F4_optional_color_blob,IDA:mmTitle_net_handle_login_response_0x16DC_reads_servernum_via_accessor18,IDA:net_build_login_request_0x1B9C_separates_n5_12_server_list_n5_4_server_select_n5_1_primary_login runtime:result4_serverinfo_then_unexpected_1_1_without_observed_1_4\n",
                 (u32)resultCode,
                 serverInfoLen,
                 colorBlobLen,
                 echoCredentials ? 1u : 0u,
                 haveUserName ? userName : "<missing>",
                 pos);
    return pos;
}

static u32 vm_net_mock_build_login_alt12_success_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    if (outCap < pos)
        return 0;

    /*
     * The live login tap currently sends a top-level 1/1/12 request. Static
     * title RE already has a dedicated wrapper for that alternate success path
     * (`sub_2A50 -> login_alt_result_dispatch -> login_stage_success_dispatch`),
     * so keep this response minimal and do not pre-inject later actorinfo here.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 12, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_login_alt12_success_response top=1,1,12 len=%u\n", pos);
    return pos;
}

static u32 vm_net_mock_build_login_actor_response(u8 requestSubtype, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 actorSubtype = vm_net_mock_resolve_login_success_subtype(requestSubtype);
    if (outCap < pos)
        return 0;

    g_netMockEnterGameOffset = 0;
    g_netMockEnterGameChecksum = 0;

    u32 actorInfoLen = 0;
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, actorSubtype, true, &actorInfoLen))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    u8 logJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u8 logSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);
    vm_net_trace("mock_login_actor_response actorinfo_len=%u top=1,1,%u job=%u sex=%u preview=%s actor=%s len=%u\n",
                 actorInfoLen,
                 actorSubtype,
                 logJob,
                 logSex,
                 vm_net_mock_actor_preview_image_name(logJob, logSex),
                 vm_net_mock_actor_resource_name(logJob, logSex),
                 pos);
    return pos;
}

static u32 vm_net_mock_build_login_role_list_only_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;

    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, 1, false, NULL))
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_net_trace("mock_login_role_list_response mode=login-success-plus-rolelist roles=1 maxroles=3 num=1 allpgs=1 len=%u\n",
                 pos);
    return pos;
}

static u32 vm_net_mock_build_login_actor_role_list_response(u8 requestSubtype, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 actorSubtype = vm_net_mock_resolve_login_success_subtype(requestSubtype);
    if (outCap < pos)
        return 0;

    u32 actorInfoLen = 0;
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, actorSubtype, true, &actorInfoLen))
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_net_trace("mock_login_role_list_response mode=actorinfo-plus-rolelist actorinfo_len=%u top=1,1,%u roles=1 maxroles=3 num=1 allpgs=1 len=%u\n",
                 actorInfoLen, actorSubtype, pos);
    return pos;
}

static u32 vm_net_mock_build_login_response(const u8 *request, u32 requestLen, u8 requestSubtype, u8 *out, u32 outCap)
{
    const char *mode = vm_net_mock_env_str("CBE_LOGIN_RESPONSE", "");
    char userName[64];
    char password[64];
    bool haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "userName", userName, sizeof(userName));
    bool havePassword = false;
    memset(password, 0, sizeof(password));
    if (!haveUserName)
        haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "username", userName, sizeof(userName));
    havePassword = vm_net_mock_get_object_string_field(request, requestLen, "password", password, sizeof(password));

    if (haveUserName && strcmp(userName, "1234") == 0)
    {
        vm_net_trace("mock_login_account_gate account=%s requestSubtype=%u result=2\n",
                     userName, requestSubtype);
        return vm_net_mock_build_login_failure_response(requestSubtype, "password error", out, outCap);
    }

    vm_net_trace("mock_login_account_gate account=%s requestSubtype=%u account_ok=1 evidence=runtime:login_request_contains_coreVer_appVer_password_imsi IDA:mmTitle_login_alt_response_dispatch_wrapper_0x2A50_dispatch_depends_on_response_result_code\n",
                 haveUserName ? userName : "<missing>",
                 requestSubtype);

    if (requestSubtype == 1 &&
        mode != NULL &&
        strcmp(mode, "serverlist-hold") == 0)
    {
        vm_net_trace("mock_login_primary_gate requestSubtype=1 result=1 reason=primary_login_wait_server_list user=%s phase=%u/%u evidence=runtime:explicit_serverlist_hold_only IDA:mmTitle_login_response_result_dispatch_0x23C0_result1_enters_success_path_and_parses_server_list\n",
                     haveUserName ? userName : "<missing>",
                     (u32)g_netMockTitleServerListPending,
                     (u32)g_netMockTitleServerSelectConfirmed);
        return vm_net_mock_build_login_primary_wait_server_response(out, outCap);
    }

    if (requestSubtype == 1 &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0 ||
         strcmp(mode, "primary-validate") == 0 ||
         strcmp(mode, "roles") == 0 ||
         strcmp(mode, "rolelist") == 0))
    {
        vm_net_trace("mock_login_primary_gate requestSubtype=1 result=1 reason=primary_login_submit user=%s phase=%u/%u evidence=runtime:latest_result4_primary_hold_caused_repeated_1_1_no_1_4,IDA:mmTitle_login_response_result_dispatch_0x23C0_result1_enters_list_screen_and_role_list_screen_render_0x31D4_emits_1_16\n",
                     haveUserName ? userName : "<missing>",
                     (u32)g_netMockTitleServerListPending,
                     (u32)g_netMockTitleServerSelectConfirmed);
        return vm_net_mock_build_login_primary_validation_response(out, outCap);
    }

    if (requestSubtype == 12 &&
        haveUserName && userName[0] == 0 &&
        havePassword && password[0] == 0 &&
        g_netMockTitleServerListPending &&
        !g_netMockTitleServerSelectConfirmed &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0))
    {
        vm_net_trace("mock_login_alt12_gate requestSubtype=12 result=1 reason=empty_credential_repeat_after_serverlist user=<empty> phase=%u/%u evidence=runtime:repeat_1_12_after_result4_left_stageFlag4_altPrompt1_serverCount1,packet:len86_username_password_empty IDA:login_alt_result_dispatch_0x19C2_result1_calls_login_stage_success_dispatch,login_stage_success_dispatch_0x1956_stageFlag4_routes_to_targetSlot4C\n",
                     (u32)g_netMockTitleServerListPending,
                     (u32)g_netMockTitleServerSelectConfirmed);
        return vm_net_mock_build_login_alt12_success_response(out, outCap);
    }

    if (requestSubtype == 12 &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0))
    {
        return vm_net_mock_build_login_alt12_server_list_response(request, requestLen, out, outCap);
    }

    if (requestSubtype == 12 &&
        (strcmp(mode, "alt12-success") == 0 ||
         strcmp(mode, "roles") == 0 ||
         strcmp(mode, "rolelist") == 0))
    {
        return vm_net_mock_build_login_alt12_success_response(out, outCap);
    }

    /* Current static/runtime reading is staged rather than same-packet mixing:
     * login validation returns only the login/server-list contract, then the
     * client sends the next request (1/1/16 for title role list, 1/1/6 for role
     * select). Retain older mixed object modes only for explicit regression
     * comparison through CBE_LOGIN_RESPONSE.
     */
    if (mode == NULL || mode[0] == 0 ||
        strcmp(mode, "staged-rolelist") == 0 ||
        strcmp(mode, "staged") == 0)
    {
        return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);
    }

    if (strcmp(mode, "roles+actor") == 0 ||
        strcmp(mode, "actor+roles") == 0 ||
        strcmp(mode, "roles-first") == 0)
    {
        return vm_net_mock_build_login_actor_role_list_response(requestSubtype, out, outCap);
    }

    if (strcmp(mode, "actor") == 0)
        return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);

    return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);
}

static u32 vm_net_mock_build_enter_game_response(u8 *out, u32 outCap)
{
    u32 pos = 11;
    if (outCap < pos)
        return 0;

    u8 payload[65535];
    const char *payloadName = "mmGameMstarWqvga.cbm";
    u32 payloadLen = vm_net_mock_load_response_file("JHOnlineData/mmGameMstarWqvga.cbm", payload, sizeof(payload));
    if (payloadLen == 0)
    {
        payloadName = "MMORPGTempcbm";
        payloadLen = vm_net_mock_load_response_file("JHOnlineData/MMORPGTempcbm", payload, sizeof(payload));
    }
    if (payloadLen == 0)
        return 0;

    if (g_netMockEnterGameOffset >= payloadLen)
    {
        g_netMockEnterGameOffset = 0;
        g_netMockEnterGameChecksum = 0;
    }

    u32 chunkLen = payloadLen - g_netMockEnterGameOffset;
    if (chunkLen > 1024)
        chunkLen = 1024;
    u32 chunkSum = vm_net_mock_signed_byte_sum(payload + g_netMockEnterGameOffset, chunkLen);
    u32 crc = g_netMockEnterGameChecksum + chunkSum;

    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "totalsize", payloadLen))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "crc", crc))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "version", (const u8 *)"\x00\x02\x00\x01", 4))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", payloadName))
        return 0;
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "data", payload + g_netMockEnterGameOffset, (u16)chunkLen))
        return 0;

    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    out[5] = 1;
    out[6] = 0x12;
    out[7] = 7;
    out[8] = 0;
    out[9] = (u8)((pos - 5) >> 8);
    out[10] = (u8)(pos - 5);
    vm_net_trace("mock_enter_game_response resource name=%s payloadLen=%u offset=%u chunkLen=%u checksumBefore=%08x chunkSum=%08x crc=%08x len=%u\n",
                 payloadName, payloadLen, g_netMockEnterGameOffset, chunkLen, g_netMockEnterGameChecksum, chunkSum, crc, pos);
    g_netMockEnterGameChecksum = crc;
    g_netMockEnterGameOffset += chunkLen;
    return pos;
}

static u32 vm_net_mock_build_game_type_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap, u8 requestType)
{
    u32 pos = 11;
    if (outCap < pos || request == NULL || requestLen < 8)
        return 0;
    u8 responseType = request[6];
    u8 responseSub = request[7];
    if (requestType == 1)
        responseSub = 0x1a;
    else if (requestType == 2)
        responseSub = 20;
    else if (requestType == 3)
        responseSub = 32;

    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (requestType == 1)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", requestType))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "npcnum", 0))
            return 0;
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", "Codex"))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "money", 0))
            return 0;
    }
    else if (requestType == 2)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "pcimg", 0))
            return 0;
    }
    else if (requestType == 3)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "expcard", 0))
            return 0;
    }

    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = request[4];
    out[5] = request[5];
    out[6] = responseType;
    out[7] = responseSub;
    out[8] = 0;
    out[9] = (u8)((pos - 5) >> 8);
    out[10] = (u8)(pos - 5);
    vm_net_trace("mock_game_type_response type=%u responseType=%u responseSub=%u scene=c00PenglaiXiandao_01 header=%02x,%02x,%02x,%02x len=%u\n",
                 requestType, responseType, responseSub, out[4], out[5], out[6], out[7], pos);
    return pos;
}

static bool vm_net_mock_object_is_split_safe(const vm_net_mock_request_object *object)
{
    if (object == NULL || object->major != 1)
        return false;

    if (object->kind == 2 && (object->subtype == 1 || object->subtype == 10))
        return true;
    if (object->kind == 4 && object->subtype == 1)
        return true;
    if (object->kind == 7 && (object->subtype == 7 || object->subtype == 18))
        return true;
    if (object->kind == 0x19 && object->subtype == 5)
        return true;
    if (object->kind == 0x63 && object->subtype == 1)
        return true;
    return false;
}

static u32 vm_net_mock_build_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap);

static u32 vm_net_mock_build_single_object_request(const vm_net_mock_request_object *object, u8 *out, u32 outCap)
{
    u32 len = 0;
    u32 objectLen = 0;
    if (object == NULL || out == NULL)
        return 0;
    objectLen = (u32)object->payloadLen + 5;
    len = (u32)object->payloadLen + 9;
    if (len > outCap || len > 0xffff)
        return 0;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(len >> 8);
    out[3] = (u8)len;
    out[4] = object->major;
    out[5] = object->kind;
    out[6] = object->subtype;
    out[7] = (u8)(objectLen >> 8);
    out[8] = (u8)objectLen;
    if (object->payloadLen != 0)
        memcpy(out + 9, object->payload, object->payloadLen);
    return len;
}

static bool vm_net_mock_append_response_objects(u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
                                                const u8 *response, u32 responseLen)
{
    u32 offset = 5;
    if (out == NULL || pos == NULL || objectCount == NULL ||
        response == NULL || responseLen < 5 || response[0] != 'W' || response[1] != 'T')
    {
        return false;
    }

    while (offset + 6 <= responseLen)
    {
        u32 objectLen = ((u32)response[offset + 4] << 8) | (u32)response[offset + 5];
        if (objectLen < 6 || offset + objectLen > responseLen)
            return false;
        if (*objectCount == 0xff || *pos + objectLen > outCap)
            return false;
        memcpy(out + *pos, response + offset, objectLen);
        *pos += objectLen;
        *objectCount += 1;
        offset += objectLen;
    }
    return offset == responseLen;
}

static u32 vm_net_mock_build_split_safe_combo_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 subCount = 0;
    vm_net_mock_request_object object;
    u8 subRequest[512];
    u8 subResponse[8192];

    if (g_netMockSplitProbe || request == NULL || requestLen < 9 || outCap < pos)
        return 0;
    if (request[0] != 'W' || request[1] != 'T')
        return 0;

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        u32 subRequestLen = 0;
        u32 subResponseLen = 0;
        if (!vm_net_mock_object_is_split_safe(&object))
            return 0;
        subRequestLen = vm_net_mock_build_single_object_request(&object, subRequest, sizeof(subRequest));
        if (subRequestLen == 0)
            return 0;
        g_netMockSplitProbe = true;
        subResponseLen = vm_net_mock_build_response(subRequest, subRequestLen, subResponse, sizeof(subResponse));
        g_netMockSplitProbe = false;
        if (subResponseLen == 0)
            return 0;
        if (!vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount, subResponse, subResponseLen))
            return 0;
        ++subCount;
    }

    if (offset != requestLen || subCount < 2 || objectCount == 0)
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_split_safe_combo_response requestObjects=%u responseObjects=%u len=%u\n",
                 subCount, objectCount, pos);
    return pos;
}

static u32 vm_net_mock_build_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    if (request == NULL || requestLen == 0 || outCap == 0)
        return 0;

    /* First principle for protocol research: only emulate server/API behavior.
     * Do not advance CBE state here by forcing return values or writing globals.
     */
    u32 hookedLen = 0;

    if (vm_net_mock_request_contains(request, requestLen, "start") &&
        (vm_net_mock_request_contains(request, requestLen, "id") ||
         vm_net_mock_request_contains(request, requestLen, "name")))
    {
        hookedLen = vm_net_mock_build_update_chunk_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-update-chunk len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-update-chunk", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_response_from_rules(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("rule", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_request_contains(request, requestLen, "maptype") &&
        vm_net_mock_request_contains(request, requestLen, "mapID") &&
        vm_net_mock_request_contains(request, requestLen, "exitID"))
    {
        hookedLen = vm_net_mock_build_scene_change_combo_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-scene-change len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-scene-change", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4))
    {
        hookedLen = vm_net_mock_build_type27_followup_combo_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-type27-followup len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-type27-followup", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x19, 5))
    {
        hookedLen = vm_net_mock_build_scene_default_event_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-scene-default-event len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-scene-default-event", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_actor_other_scene_default_combo_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_scene_default_event_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-actor-other-scene-default-combo len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-actor-other-scene-default-combo", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x63, 1))
    {
        hookedLen = vm_net_mock_build_short_wt_control_echo_response(request, requestLen, 0x63, 1, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-short-63-1-echo len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-short-63-1-echo", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 7, 18))
    {
        hookedLen = vm_net_mock_build_practise_info18_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-practise-info18 len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-practise-info18", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_battle_death_prompt_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-battle-death-prompt-followup len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-battle-death-prompt-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_actor_query14_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-shop-actor-query14 len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-shop-actor-query14", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_resource_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-scene-resource-followup len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-scene-resource-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_task_subset_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-scene-task-subset-followup len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-scene-task-subset-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_interaction_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-scene-interaction-followup len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-scene-interaction-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_actor_moveinfo_ack_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-actor-moveinfo-ack len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-actor-moveinfo-ack", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_actor_moveinfo_name_update_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-actor-moveinfo-name-update len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-actor-moveinfo-name-update", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_special_scene_interaction_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-special-scene-interaction len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-special-scene-interaction", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_battle_operate_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-battle-operate len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-battle-operate", request, requestLen, hookedLen);
        return hookedLen;
    }
    hookedLen = vm_net_mock_build_battle_operate_response_fallback(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-battle-operate-fallback len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-battle-operate-fallback", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_battle_auto12_ack_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-battle-auto12-ack len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-battle-auto12-ack", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_challenge_interaction_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-challenge-interaction len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-challenge-interaction", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_actor_other_only10_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_actor_other_portal_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-actor-other-portal len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-actor-other-portal", request, requestLen, hookedLen);
            return hookedLen;
        }

        hookedLen = vm_net_mock_build_actor_other_only10_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-actor-other-only10 len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-actor-other-only10", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_login_tail_skill_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_login_tail_skill_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-login-tail-skill len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-login-tail-skill", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_title_server_select_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_title_server_select_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-title-server-select len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-title-server-select", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    u8 requestKind = 0;
    u8 requestSubtype = 0;
    if (vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &requestKind, &requestSubtype))
    {
        if (vm_net_mock_is_title_role_select_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_title_role_select_response(request, requestLen, out, outCap);
            if (hookedLen)
            {
                vm_net_trace("mock_default source=builtin-title-role-select len=%u\n", hookedLen);
                vm_net_log_handled_packet("builtin-title-role-select", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (vm_net_mock_is_title_rolelist_stage_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_title_rolelist_wait_server_select_response(out, outCap);
            if (hookedLen)
            {
                vm_net_trace("mock_default source=builtin-title-rolelist-wait-server-select len=%u\n", hookedLen);
                vm_net_log_handled_packet("builtin-title-rolelist-wait-server-select", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (requestKind == 0x0a && requestSubtype == 0x20)
        {
            hookedLen = vm_net_mock_build_role_list_response(out, outCap);
            if (hookedLen)
            {
                vm_net_trace("mock_default source=builtin-role-list len=%u\n", hookedLen);
                vm_net_log_handled_packet("builtin-role-list", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
    }

    if (vm_net_mock_is_login_validation_request(request, requestLen, &requestSubtype))
    {
        vm_net_ensure_business_callback("login-request");
        hookedLen = vm_net_mock_load_response_file("net_mocks/login.bin", out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=file-login len=%u\n", hookedLen);
            vm_net_log_handled_packet("file-login", request, requestLen, hookedLen);
            return hookedLen;
        }
        hookedLen = vm_net_mock_build_login_response(request, requestLen, requestSubtype, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-login len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-login", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    u8 gameRequestType = 0;
    hookedLen = vm_net_mock_build_group_type1_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-group-type1 len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-group-type1", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_get_object_u8_field(request, requestLen, "type", &gameRequestType) &&
        (gameRequestType == 1 || gameRequestType == 2 || gameRequestType == 3))
    {
        hookedLen = vm_net_mock_build_game_type_response(request, requestLen, out, outCap, gameRequestType);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-game-type type=%u len=%u\n", gameRequestType, hookedLen);
            vm_net_log_handled_packet("builtin-game-type", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_request_contains(request, requestLen, "version"))
    {
        vm_net_mock_title_login_phase_reset("version-handshake");
        hookedLen = vm_net_mock_build_version_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-version len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-version", request, requestLen, hookedLen);
            return hookedLen;
        }
    }
    hookedLen = vm_net_mock_build_split_safe_combo_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-split-safe-combo len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-split-safe-combo", request, requestLen, hookedLen);
        return hookedLen;
    }
    if (requestKind == 4 && requestSubtype == 2)
    {
        hookedLen = vm_net_mock_build_battle_operate_response_fallback(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-battle-operate-lastchance-fallback len=%u evidence=runtime:header_4_2_reached_unhandled_exit_so_lastchance_builder_claims_it_before_assert\n",
                         hookedLen);
            vm_net_log_handled_packet("builtin-battle-operate-lastchance-fallback", request, requestLen, hookedLen);
            return hookedLen;
        }
        hookedLen = vm_net_mock_build_battle_operate_response_raw82(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-battle-operate-raw82 len=%u evidence=runtime:header_4_2_still_failed_all_dynamic_builders_so_emit_known_wire_valid_raw82_response_before_assert\n",
                         hookedLen);
            vm_net_log_handled_packet("builtin-battle-operate-raw82", request, requestLen, hookedLen);
            return hookedLen;
        }
    }
    vm_net_log_unhandled_packet(request, requestLen);
    assert(0);
    return 0;
}

static u32 vm_net_mock_sync_response_to_vm(void)
{
    if (g_netMockResponseLen == 0)
        return 0;

    g_netMockResponseVmPtr = vm_malloc(g_netMockResponseLen);
    if (g_netMockResponseVmPtr == 0)
        return 0;
    uc_mem_write(MTK, g_netMockResponseVmPtr, g_netMockResponse, g_netMockResponseLen);
    return g_netMockResponseVmPtr;
}

static u32 vm_net_mock_sync_buffer_to_vm(const u8 *buffer, u32 bufferLen)
{
    u32 responsePtr = 0;
    if (buffer == NULL || bufferLen == 0)
        return 0;
    responsePtr = vm_malloc(bufferLen);
    if (responsePtr == 0)
        return 0;
    uc_mem_write(MTK, responsePtr, buffer, bufferLen);
    return responsePtr;
}

static void vm_net_mock_on_send(u32 connectId, u32 dataPtr, u32 dataLen)
{
    if (dataPtr == 0 || dataLen == 0)
        return;

    g_netLastHandledValid = 0;
    g_netLastHandledResponseLen = 0;
    g_netLastHandledSource[0] = 0;
    g_netLastHandledSummary[0] = 0;

    u8 request[512];
    u32 readLen = dataLen < sizeof(request) ? dataLen : sizeof(request);
    uc_mem_read(MTK, dataPtr, request, readLen);
    vm_net_trace("send connect=%u dataPtr=%08x dataLen=%u readLen=%u\n", connectId, dataPtr, dataLen, readLen);
    vm_net_trace_outgoing_wt_send_context(request, readLen, connectId, dataPtr, dataLen);
    bool closeAfterData = vm_net_mock_request_contains(request, readLen, "version") &&
                          !vm_net_mock_request_contains(request, readLen, "start") &&
                          vm_net_mock_has_installed_update();
    /*
     * Empty 25/5 scene-default sends are emitted from inside the client's wait
     * callback. Dispatching the response reentrantly lets the callback clear
     * R9+0x5531 before the send wrapper writes it back to 1, leaving later WT
     * requests blocked. Queue it like a normal async network response instead.
     */
    bool immediateFlushAfterData = false;
    u32 responseEventType = 7;

    g_netMockResponseLen = vm_net_mock_build_response(request, readLen, g_netMockResponse, sizeof(g_netMockResponse));
    g_netMockResponseOffset = 0;
    g_netUpLinkData += dataLen;
    if (g_netMockResponseLen == 0)
        return;

    u32 responsePtr = vm_net_mock_sync_response_to_vm();
    if (responsePtr == 0)
        return;
    g_netDownLinkData += g_netMockResponseLen;
    vm_net_trace("mock_response ptr=%08x len=%u\n", responsePtr, g_netMockResponseLen);
    vm_net_packet_log_exchange(request, readLen, g_netMockResponse, g_netMockResponseLen);

    vm_net_channel *channel = scheduler_find_net_channel(connectId);
    if (channel && channel->callback)
    {
        vm_net_trace("queue_data_event connect=%u cb=%08x ctx=%08x event=%u len=%u\n", connectId, channel->callback, channel->context, responseEventType, g_netMockResponseLen);
        scheduler_queue_net_event(responseEventType, responsePtr, g_netMockResponseLen, g_netMockResponseLen, channel->callback, channel->context);
        if (immediateFlushAfterData && g_netTaskDispatchDepth == 0)
        {
            vm_net_trace("queue_data_event_immediate_flush connect=%u cb=%08x ctx=%08x len=%u tick=%u\n",
                         connectId, channel->callback, channel->context, g_netMockResponseLen, g_schedulerTick);
            uc_err flushErr = scheduler_dispatch_net_tasks();
            if (flushErr != UC_ERR_OK)
                vm_net_trace("queue_data_event_immediate_flush_error err=%u\n", flushErr);
        }
        if (closeAfterData)
        {
            vm_net_trace("queue_close_event connect=%u cb=%08x ctx=%08x event=9\n", connectId, channel->callback, channel->context);
            scheduler_queue_net_event(9, 0, 0, 0, channel->callback, channel->context);
        }
    }
    else
    {
        vm_net_trace("queue_data_event_miss connect=%u\n", connectId);
    }
}

static u32 vm_net_mock_read_data(u32 dst, u32 dstLen)
{
    if (dst == 0 || dstLen == 0 || g_netMockResponseOffset >= g_netMockResponseLen)
    {
        vm_net_trace("getdata dst=%08x dstLen=%u copied=0 offset=%u len=%u\n", dst, dstLen, g_netMockResponseOffset, g_netMockResponseLen);
        return vm_set_call_result(0);
    }

    u32 remain = g_netMockResponseLen - g_netMockResponseOffset;
    u32 copyLen = dstLen < remain ? dstLen : remain;
    uc_mem_write(MTK, dst, g_netMockResponse + g_netMockResponseOffset, copyLen);
    g_netMockResponseOffset += copyLen;
    vm_net_trace("getdata dst=%08x dstLen=%u copied=%u offset=%u len=%u\n", dst, dstLen, copyLen, g_netMockResponseOffset, g_netMockResponseLen);
    return vm_set_call_result(copyLen);
}

