/*
 * Copyright 2017 Alistair Leslie-Hughes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS

#include "mfplat_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

struct media_type
{
    struct attributes attributes;
    IMFMediaType IMFMediaType_iface;
};

struct stream_desc
{
    struct attributes attributes;
    IMFStreamDescriptor IMFStreamDescriptor_iface;
    IMFMediaTypeHandler IMFMediaTypeHandler_iface;
    DWORD identifier;
    IMFMediaType **media_types;
    unsigned int media_types_count;
    IMFMediaType *current_type;
    CRITICAL_SECTION cs;
};

struct presentation_desc_entry
{
    IMFStreamDescriptor *descriptor;
    BOOL selected;
};

struct presentation_desc
{
    struct attributes attributes;
    IMFPresentationDescriptor IMFPresentationDescriptor_iface;
    struct presentation_desc_entry *descriptors;
    unsigned int count;
    CRITICAL_SECTION cs;
};

static HRESULT presentation_descriptor_init(struct presentation_desc *object, DWORD count);

static inline struct media_type *impl_from_IMFMediaType(IMFMediaType *iface)
{
    return CONTAINING_RECORD(iface, struct media_type, IMFMediaType_iface);
}

static inline struct stream_desc *impl_from_IMFStreamDescriptor(IMFStreamDescriptor *iface)
{
    return CONTAINING_RECORD(iface, struct stream_desc, IMFStreamDescriptor_iface);
}

static struct stream_desc *impl_from_IMFMediaTypeHandler(IMFMediaTypeHandler *iface)
{
    return CONTAINING_RECORD(iface, struct stream_desc, IMFMediaTypeHandler_iface);
}

static struct presentation_desc *impl_from_IMFPresentationDescriptor(IMFPresentationDescriptor *iface)
{
    return CONTAINING_RECORD(iface, struct presentation_desc, IMFPresentationDescriptor_iface);
}

static HRESULT WINAPI mediatype_QueryInterface(IMFMediaType *iface, REFIID riid, void **out)
{
    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), out);

    if (IsEqualIID(riid, &IID_IMFMediaType) ||
            IsEqualIID(riid, &IID_IMFAttributes) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *out = iface;
        IMFMediaType_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported %s.\n", debugstr_guid(riid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI mediatype_AddRef(IMFMediaType *iface)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    ULONG refcount = InterlockedIncrement(&media_type->attributes.ref);

    TRACE("%p, refcount %u.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI mediatype_Release(IMFMediaType *iface)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    ULONG refcount = InterlockedDecrement(&media_type->attributes.ref);

    TRACE("%p, refcount %u.\n", iface, refcount);

    if (!refcount)
    {
        heap_free(media_type);
    }

    return refcount;
}

static HRESULT WINAPI mediatype_GetItem(IMFMediaType *iface, REFGUID key, PROPVARIANT *value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetItem(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_GetItemType(IMFMediaType *iface, REFGUID key, MF_ATTRIBUTE_TYPE *type)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetItemType(&media_type->attributes.IMFAttributes_iface, key, type);
}

static HRESULT WINAPI mediatype_CompareItem(IMFMediaType *iface, REFGUID key, REFPROPVARIANT value, BOOL *result)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_CompareItem(&media_type->attributes.IMFAttributes_iface, key, value, result);
}

static HRESULT WINAPI mediatype_Compare(IMFMediaType *iface, IMFAttributes *attrs, MF_ATTRIBUTES_MATCH_TYPE type,
                BOOL *result)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_Compare(&media_type->attributes.IMFAttributes_iface, attrs, type, result);
}

static HRESULT WINAPI mediatype_GetUINT32(IMFMediaType *iface, REFGUID key, UINT32 *value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetUINT32(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_GetUINT64(IMFMediaType *iface, REFGUID key, UINT64 *value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetUINT64(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_GetDouble(IMFMediaType *iface, REFGUID key, double *value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetDouble(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_GetGUID(IMFMediaType *iface, REFGUID key, GUID *value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetGUID(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_GetStringLength(IMFMediaType *iface, REFGUID key, UINT32 *length)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetStringLength(&media_type->attributes.IMFAttributes_iface, key, length);
}

static HRESULT WINAPI mediatype_GetString(IMFMediaType *iface, REFGUID key, WCHAR *value,
        UINT32 size, UINT32 *length)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetString(&media_type->attributes.IMFAttributes_iface, key, value, size, length);
}

static HRESULT WINAPI mediatype_GetAllocatedString(IMFMediaType *iface, REFGUID key,
        WCHAR **value, UINT32 *length)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetAllocatedString(&media_type->attributes.IMFAttributes_iface, key, value, length);
}

static HRESULT WINAPI mediatype_GetBlobSize(IMFMediaType *iface, REFGUID key, UINT32 *size)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetBlobSize(&media_type->attributes.IMFAttributes_iface, key, size);
}

static HRESULT WINAPI mediatype_GetBlob(IMFMediaType *iface, REFGUID key, UINT8 *buf,
        UINT32 bufsize, UINT32 *blobsize)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetBlob(&media_type->attributes.IMFAttributes_iface, key, buf, bufsize, blobsize);
}

static HRESULT WINAPI mediatype_GetAllocatedBlob(IMFMediaType *iface, REFGUID key, UINT8 **buf, UINT32 *size)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetAllocatedBlob(&media_type->attributes.IMFAttributes_iface, key, buf, size);
}

static HRESULT WINAPI mediatype_GetUnknown(IMFMediaType *iface, REFGUID key, REFIID riid, void **ppv)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetUnknown(&media_type->attributes.IMFAttributes_iface, key, riid, ppv);
}

static HRESULT WINAPI mediatype_SetItem(IMFMediaType *iface, REFGUID key, REFPROPVARIANT value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_SetItem(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_DeleteItem(IMFMediaType *iface, REFGUID key)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_DeleteItem(&media_type->attributes.IMFAttributes_iface, key);
}

static HRESULT WINAPI mediatype_DeleteAllItems(IMFMediaType *iface)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_DeleteAllItems(&media_type->attributes.IMFAttributes_iface);
}

static HRESULT WINAPI mediatype_SetUINT32(IMFMediaType *iface, REFGUID key, UINT32 value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_SetUINT32(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_SetUINT64(IMFMediaType *iface, REFGUID key, UINT64 value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_SetUINT64(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_SetDouble(IMFMediaType *iface, REFGUID key, double value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_SetDouble(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_SetGUID(IMFMediaType *iface, REFGUID key, REFGUID value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_SetGUID(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_SetString(IMFMediaType *iface, REFGUID key, const WCHAR *value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_SetString(&media_type->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI mediatype_SetBlob(IMFMediaType *iface, REFGUID key, const UINT8 *buf, UINT32 size)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_SetBlob(&media_type->attributes.IMFAttributes_iface, key, buf, size);
}

static HRESULT WINAPI mediatype_SetUnknown(IMFMediaType *iface, REFGUID key, IUnknown *unknown)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_SetUnknown(&media_type->attributes.IMFAttributes_iface, key, unknown);
}

static HRESULT WINAPI mediatype_LockStore(IMFMediaType *iface)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_LockStore(&media_type->attributes.IMFAttributes_iface);
}

static HRESULT WINAPI mediatype_UnlockStore(IMFMediaType *iface)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_UnlockStore(&media_type->attributes.IMFAttributes_iface);
}

static HRESULT WINAPI mediatype_GetCount(IMFMediaType *iface, UINT32 *items)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetCount(&media_type->attributes.IMFAttributes_iface, items);
}

static HRESULT WINAPI mediatype_GetItemByIndex(IMFMediaType *iface, UINT32 index, GUID *key, PROPVARIANT *value)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_GetItemByIndex(&media_type->attributes.IMFAttributes_iface, index, key, value);
}

static HRESULT WINAPI mediatype_CopyAllItems(IMFMediaType *iface, IMFAttributes *dest)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    return IMFAttributes_CopyAllItems(&media_type->attributes.IMFAttributes_iface, dest);
}

static HRESULT WINAPI mediatype_GetMajorType(IMFMediaType *iface, GUID *guid)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    TRACE("%p, %p.\n", iface, guid);
    return IMFAttributes_GetGUID(&media_type->attributes.IMFAttributes_iface, &MF_MT_MAJOR_TYPE, guid);
}

static HRESULT WINAPI mediatype_IsCompressedFormat(IMFMediaType *iface, BOOL *compressed)
{
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    UINT32 value;
    HRESULT hr;

    TRACE("%p, %p.\n", iface, compressed);

    hr = IMFAttributes_GetUINT32(&media_type->attributes.IMFAttributes_iface, &MF_MT_ALL_SAMPLES_INDEPENDENT, &value);
    if (FAILED(hr))
        value = 0;

    *compressed = !value;

    return hr;
}

static HRESULT WINAPI mediatype_IsEqual(IMFMediaType *iface, IMFMediaType *type, DWORD *flags)
{
    const DWORD full_equality_flags = MF_MEDIATYPE_EQUAL_MAJOR_TYPES | MF_MEDIATYPE_EQUAL_FORMAT_TYPES |
            MF_MEDIATYPE_EQUAL_FORMAT_DATA | MF_MEDIATYPE_EQUAL_FORMAT_USER_DATA;
    struct media_type *media_type = impl_from_IMFMediaType(iface);
    struct comparand
    {
        IMFAttributes *type;
        PROPVARIANT value;
        UINT32 count;
        GUID guid;
        HRESULT hr;
    } left, right, swp;
    unsigned int i;
    BOOL result;

    TRACE("%p, %p, %p.\n", iface, type, flags);

    *flags = 0;

    left.type = &media_type->attributes.IMFAttributes_iface;
    right.type = (IMFAttributes *)type;

    if (FAILED(IMFAttributes_GetGUID(left.type, &MF_MT_MAJOR_TYPE, &left.guid)))
        return E_INVALIDARG;

    if (FAILED(IMFAttributes_GetGUID(right.type, &MF_MT_MAJOR_TYPE, &right.guid)))
        return E_INVALIDARG;

    if (IsEqualGUID(&left.guid, &right.guid))
        *flags |= MF_MEDIATYPE_EQUAL_MAJOR_TYPES;

    /* Subtypes equal or both missing. */
    left.hr = IMFAttributes_GetGUID(left.type, &MF_MT_SUBTYPE, &left.guid);
    right.hr = IMFAttributes_GetGUID(right.type, &MF_MT_SUBTYPE, &right.guid);

    if ((SUCCEEDED(left.hr) && SUCCEEDED(right.hr) && IsEqualGUID(&left.guid, &right.guid)) ||
           (FAILED(left.hr) && FAILED(right.hr)))
    {
        *flags |= MF_MEDIATYPE_EQUAL_FORMAT_TYPES;
    }

    /* Format data */
    IMFAttributes_GetCount(left.type, &left.count);
    IMFAttributes_GetCount(right.type, &right.count);

    if (right.count < left.count)
    {
        swp = left;
        left = right;
        right = swp;
    }

    *flags |= MF_MEDIATYPE_EQUAL_FORMAT_DATA;

    for (i = 0; i < left.count; ++i)
    {
        PROPVARIANT value;
        GUID key;

        if (SUCCEEDED(IMFAttributes_GetItemByIndex(left.type, i, &key, &value)))
        {
            if (IsEqualGUID(&key, &MF_MT_USER_DATA) ||
                    IsEqualGUID(&key, &MF_MT_FRAME_RATE_RANGE_MIN) ||
                    IsEqualGUID(&key, &MF_MT_FRAME_RATE_RANGE_MAX))
            {
                PropVariantClear(&value);
                continue;
            }

            result = FALSE;
            IMFAttributes_CompareItem(right.type, &key, &value, &result);
            PropVariantClear(&value);
            if (!result)
            {
                *flags &= ~MF_MEDIATYPE_EQUAL_FORMAT_DATA;
                break;
            }
        }
    }

    /* User data */
    PropVariantInit(&left.value);
    left.hr = IMFAttributes_GetItem(left.type, &MF_MT_USER_DATA, &left.value);
    PropVariantInit(&right.value);
    right.hr = IMFAttributes_GetItem(right.type, &MF_MT_USER_DATA, &right.value);

    if (SUCCEEDED(left.hr) && SUCCEEDED(left.hr))
    {
        result = FALSE;
        IMFAttributes_CompareItem(left.type, &MF_MT_USER_DATA, &left.value, &result);
    }
    else if (FAILED(left.hr) && FAILED(left.hr))
        result = TRUE;

    PropVariantClear(&left.value);
    PropVariantClear(&right.value);

    if (result)
        *flags |= MF_MEDIATYPE_EQUAL_FORMAT_USER_DATA;

    return *flags == full_equality_flags ? S_OK : S_FALSE;
}

static HRESULT WINAPI mediatype_GetRepresentation(IMFMediaType *iface, GUID guid, void **representation)
{
    FIXME("%p, %s, %p.\n", iface, debugstr_guid(&guid), representation);

    return E_NOTIMPL;
}

static HRESULT WINAPI mediatype_FreeRepresentation(IMFMediaType *iface, GUID guid, void *representation)
{
    FIXME("%p, %s, %p.\n", iface, debugstr_guid(&guid), representation);

    return E_NOTIMPL;
}

static const IMFMediaTypeVtbl mediatypevtbl =
{
    mediatype_QueryInterface,
    mediatype_AddRef,
    mediatype_Release,
    mediatype_GetItem,
    mediatype_GetItemType,
    mediatype_CompareItem,
    mediatype_Compare,
    mediatype_GetUINT32,
    mediatype_GetUINT64,
    mediatype_GetDouble,
    mediatype_GetGUID,
    mediatype_GetStringLength,
    mediatype_GetString,
    mediatype_GetAllocatedString,
    mediatype_GetBlobSize,
    mediatype_GetBlob,
    mediatype_GetAllocatedBlob,
    mediatype_GetUnknown,
    mediatype_SetItem,
    mediatype_DeleteItem,
    mediatype_DeleteAllItems,
    mediatype_SetUINT32,
    mediatype_SetUINT64,
    mediatype_SetDouble,
    mediatype_SetGUID,
    mediatype_SetString,
    mediatype_SetBlob,
    mediatype_SetUnknown,
    mediatype_LockStore,
    mediatype_UnlockStore,
    mediatype_GetCount,
    mediatype_GetItemByIndex,
    mediatype_CopyAllItems,
    mediatype_GetMajorType,
    mediatype_IsCompressedFormat,
    mediatype_IsEqual,
    mediatype_GetRepresentation,
    mediatype_FreeRepresentation
};

/***********************************************************************
 *      MFCreateMediaType (mfplat.@)
 */
HRESULT WINAPI MFCreateMediaType(IMFMediaType **media_type)
{
    struct media_type *object;

    TRACE("%p.\n", media_type);

    if (!media_type)
        return E_INVALIDARG;

    object = heap_alloc(sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;

    init_attribute_object(&object->attributes, 0);
    object->IMFMediaType_iface.lpVtbl = &mediatypevtbl;

    *media_type = &object->IMFMediaType_iface;

    return S_OK;
}


static HRESULT WINAPI stream_descriptor_QueryInterface(IMFStreamDescriptor *iface, REFIID riid, void **out)
{
    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), out);

    if (IsEqualIID(riid, &IID_IMFStreamDescriptor) ||
            IsEqualIID(riid, &IID_IMFAttributes) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *out = iface;
        IMFStreamDescriptor_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported %s.\n", debugstr_guid(riid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI stream_descriptor_AddRef(IMFStreamDescriptor *iface)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    ULONG refcount = InterlockedIncrement(&stream_desc->attributes.ref);

    TRACE("%p, refcount %u.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI stream_descriptor_Release(IMFStreamDescriptor *iface)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    ULONG refcount = InterlockedDecrement(&stream_desc->attributes.ref);
    unsigned int i;

    TRACE("%p, refcount %u.\n", iface, refcount);

    if (!refcount)
    {
        for (i = 0; i < stream_desc->media_types_count; ++i)
        {
            if (stream_desc->media_types[i])
                IMFMediaType_Release(stream_desc->media_types[i]);
        }
        heap_free(stream_desc->media_types);
        if (stream_desc->current_type)
            IMFMediaType_Release(stream_desc->current_type);
        DeleteCriticalSection(&stream_desc->cs);
        heap_free(stream_desc);
    }

    return refcount;
}

static HRESULT WINAPI stream_descriptor_GetItem(IMFStreamDescriptor *iface, REFGUID key, PROPVARIANT *value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetItem(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_GetItemType(IMFStreamDescriptor *iface, REFGUID key, MF_ATTRIBUTE_TYPE *type)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetItemType(&stream_desc->attributes.IMFAttributes_iface, key, type);
}

static HRESULT WINAPI stream_descriptor_CompareItem(IMFStreamDescriptor *iface, REFGUID key, REFPROPVARIANT value,
        BOOL *result)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_CompareItem(&stream_desc->attributes.IMFAttributes_iface, key, value, result);
}

static HRESULT WINAPI stream_descriptor_Compare(IMFStreamDescriptor *iface, IMFAttributes *theirs,
        MF_ATTRIBUTES_MATCH_TYPE type, BOOL *result)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_Compare(&stream_desc->attributes.IMFAttributes_iface, theirs, type, result);
}

static HRESULT WINAPI stream_descriptor_GetUINT32(IMFStreamDescriptor *iface, REFGUID key, UINT32 *value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetUINT32(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_GetUINT64(IMFStreamDescriptor *iface, REFGUID key, UINT64 *value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetUINT64(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_GetDouble(IMFStreamDescriptor *iface, REFGUID key, double *value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetDouble(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_GetGUID(IMFStreamDescriptor *iface, REFGUID key, GUID *value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetGUID(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_GetStringLength(IMFStreamDescriptor *iface, REFGUID key, UINT32 *length)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetStringLength(&stream_desc->attributes.IMFAttributes_iface, key, length);
}

static HRESULT WINAPI stream_descriptor_GetString(IMFStreamDescriptor *iface, REFGUID key, WCHAR *value,
        UINT32 size, UINT32 *length)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetString(&stream_desc->attributes.IMFAttributes_iface, key, value, size, length);
}

static HRESULT WINAPI stream_descriptor_GetAllocatedString(IMFStreamDescriptor *iface, REFGUID key,
        WCHAR **value, UINT32 *length)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetAllocatedString(&stream_desc->attributes.IMFAttributes_iface, key, value, length);
}

static HRESULT WINAPI stream_descriptor_GetBlobSize(IMFStreamDescriptor *iface, REFGUID key, UINT32 *size)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetBlobSize(&stream_desc->attributes.IMFAttributes_iface, key, size);
}

static HRESULT WINAPI stream_descriptor_GetBlob(IMFStreamDescriptor *iface, REFGUID key, UINT8 *buf,
        UINT32 bufsize, UINT32 *blobsize)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetBlob(&stream_desc->attributes.IMFAttributes_iface, key, buf, bufsize, blobsize);
}

static HRESULT WINAPI stream_descriptor_GetAllocatedBlob(IMFStreamDescriptor *iface, REFGUID key, UINT8 **buf,
        UINT32 *size)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetAllocatedBlob(&stream_desc->attributes.IMFAttributes_iface, key, buf, size);
}

static HRESULT WINAPI stream_descriptor_GetUnknown(IMFStreamDescriptor *iface, REFGUID key, REFIID riid, void **ppv)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetUnknown(&stream_desc->attributes.IMFAttributes_iface, key, riid, ppv);
}

static HRESULT WINAPI stream_descriptor_SetItem(IMFStreamDescriptor *iface, REFGUID key, REFPROPVARIANT value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_SetItem(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_DeleteItem(IMFStreamDescriptor *iface, REFGUID key)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_DeleteItem(&stream_desc->attributes.IMFAttributes_iface, key);
}

static HRESULT WINAPI stream_descriptor_DeleteAllItems(IMFStreamDescriptor *iface)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_DeleteAllItems(&stream_desc->attributes.IMFAttributes_iface);
}

static HRESULT WINAPI stream_descriptor_SetUINT32(IMFStreamDescriptor *iface, REFGUID key, UINT32 value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_SetUINT32(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_SetUINT64(IMFStreamDescriptor *iface, REFGUID key, UINT64 value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_SetUINT64(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_SetDouble(IMFStreamDescriptor *iface, REFGUID key, double value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_SetDouble(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_SetGUID(IMFStreamDescriptor *iface, REFGUID key, REFGUID value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_SetGUID(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_SetString(IMFStreamDescriptor *iface, REFGUID key, const WCHAR *value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_SetString(&stream_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI stream_descriptor_SetBlob(IMFStreamDescriptor *iface, REFGUID key, const UINT8 *buf, UINT32 size)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_SetBlob(&stream_desc->attributes.IMFAttributes_iface, key, buf, size);
}

static HRESULT WINAPI stream_descriptor_SetUnknown(IMFStreamDescriptor *iface, REFGUID key, IUnknown *unknown)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_SetUnknown(&stream_desc->attributes.IMFAttributes_iface, key, unknown);
}

static HRESULT WINAPI stream_descriptor_LockStore(IMFStreamDescriptor *iface)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_LockStore(&stream_desc->attributes.IMFAttributes_iface);
}

static HRESULT WINAPI stream_descriptor_UnlockStore(IMFStreamDescriptor *iface)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_UnlockStore(&stream_desc->attributes.IMFAttributes_iface);
}

static HRESULT WINAPI stream_descriptor_GetCount(IMFStreamDescriptor *iface, UINT32 *items)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetCount(&stream_desc->attributes.IMFAttributes_iface, items);
}

static HRESULT WINAPI stream_descriptor_GetItemByIndex(IMFStreamDescriptor *iface, UINT32 index, GUID *key, PROPVARIANT *value)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_GetItemByIndex(&stream_desc->attributes.IMFAttributes_iface, index, key, value);
}

static HRESULT WINAPI stream_descriptor_CopyAllItems(IMFStreamDescriptor *iface, IMFAttributes *dest)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);
    return IMFAttributes_CopyAllItems(&stream_desc->attributes.IMFAttributes_iface, dest);
}

static HRESULT WINAPI stream_descriptor_GetStreamIdentifier(IMFStreamDescriptor *iface, DWORD *identifier)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);

    TRACE("%p, %p.\n", iface, identifier);

    *identifier = stream_desc->identifier;

    return S_OK;
}

static HRESULT WINAPI stream_descriptor_GetMediaTypeHandler(IMFStreamDescriptor *iface, IMFMediaTypeHandler **handler)
{
    struct stream_desc *stream_desc = impl_from_IMFStreamDescriptor(iface);

    TRACE("%p, %p.\n", iface, handler);

    *handler = &stream_desc->IMFMediaTypeHandler_iface;
    IMFStreamDescriptor_AddRef(iface);

    return S_OK;
}

static const IMFStreamDescriptorVtbl streamdescriptorvtbl =
{
    stream_descriptor_QueryInterface,
    stream_descriptor_AddRef,
    stream_descriptor_Release,
    stream_descriptor_GetItem,
    stream_descriptor_GetItemType,
    stream_descriptor_CompareItem,
    stream_descriptor_Compare,
    stream_descriptor_GetUINT32,
    stream_descriptor_GetUINT64,
    stream_descriptor_GetDouble,
    stream_descriptor_GetGUID,
    stream_descriptor_GetStringLength,
    stream_descriptor_GetString,
    stream_descriptor_GetAllocatedString,
    stream_descriptor_GetBlobSize,
    stream_descriptor_GetBlob,
    stream_descriptor_GetAllocatedBlob,
    stream_descriptor_GetUnknown,
    stream_descriptor_SetItem,
    stream_descriptor_DeleteItem,
    stream_descriptor_DeleteAllItems,
    stream_descriptor_SetUINT32,
    stream_descriptor_SetUINT64,
    stream_descriptor_SetDouble,
    stream_descriptor_SetGUID,
    stream_descriptor_SetString,
    stream_descriptor_SetBlob,
    stream_descriptor_SetUnknown,
    stream_descriptor_LockStore,
    stream_descriptor_UnlockStore,
    stream_descriptor_GetCount,
    stream_descriptor_GetItemByIndex,
    stream_descriptor_CopyAllItems,
    stream_descriptor_GetStreamIdentifier,
    stream_descriptor_GetMediaTypeHandler
};

static HRESULT WINAPI mediatype_handler_QueryInterface(IMFMediaTypeHandler *iface, REFIID riid, void **obj)
{
    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IMFMediaTypeHandler) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IMFMediaTypeHandler_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported %s.\n", debugstr_guid(riid));
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI mediatype_handler_AddRef(IMFMediaTypeHandler *iface)
{
    struct stream_desc *stream_desc = impl_from_IMFMediaTypeHandler(iface);
    return IMFStreamDescriptor_AddRef(&stream_desc->IMFStreamDescriptor_iface);
}

static ULONG WINAPI mediatype_handler_Release(IMFMediaTypeHandler *iface)
{
    struct stream_desc *stream_desc = impl_from_IMFMediaTypeHandler(iface);
    return IMFStreamDescriptor_Release(&stream_desc->IMFStreamDescriptor_iface);
}

static HRESULT WINAPI mediatype_handler_IsMediaTypeSupported(IMFMediaTypeHandler *iface, IMFMediaType *in_type,
        IMFMediaType **out_type)
{
    FIXME("%p, %p, %p.\n", iface, in_type, out_type);

    return E_NOTIMPL;
}

static HRESULT WINAPI mediatype_handler_GetMediaTypeCount(IMFMediaTypeHandler *iface, DWORD *count)
{
    struct stream_desc *stream_desc = impl_from_IMFMediaTypeHandler(iface);

    TRACE("%p, %p.\n", iface, count);

    *count = stream_desc->media_types_count;

    return S_OK;
}

static HRESULT WINAPI mediatype_handler_GetMediaTypeByIndex(IMFMediaTypeHandler *iface, DWORD index,
        IMFMediaType **type)
{
    struct stream_desc *stream_desc = impl_from_IMFMediaTypeHandler(iface);

    TRACE("%p, %u, %p.\n", iface, index, type);

    if (index >= stream_desc->media_types_count)
        return MF_E_NO_MORE_TYPES;

    if (stream_desc->media_types[index])
    {
        *type = stream_desc->media_types[index];
        IMFMediaType_AddRef(*type);
    }

    return stream_desc->media_types[index] ? S_OK : E_FAIL;
}

static HRESULT WINAPI mediatype_handler_SetCurrentMediaType(IMFMediaTypeHandler *iface, IMFMediaType *type)
{
    struct stream_desc *stream_desc = impl_from_IMFMediaTypeHandler(iface);

    TRACE("%p, %p.\n", iface, type);

    if (!type)
        return E_POINTER;

    EnterCriticalSection(&stream_desc->cs);
    if (stream_desc->current_type)
        IMFMediaType_Release(stream_desc->current_type);
    stream_desc->current_type = type;
    IMFMediaType_AddRef(stream_desc->current_type);
    LeaveCriticalSection(&stream_desc->cs);

    return S_OK;
}

static HRESULT WINAPI mediatype_handler_GetCurrentMediaType(IMFMediaTypeHandler *iface, IMFMediaType **type)
{
    struct stream_desc *stream_desc = impl_from_IMFMediaTypeHandler(iface);
    HRESULT hr = S_OK;

    TRACE("%p, %p.\n", iface, type);

    EnterCriticalSection(&stream_desc->cs);
    if (stream_desc->current_type)
    {
        *type = stream_desc->current_type;
        IMFMediaType_AddRef(*type);
    }
    else
        hr = MF_E_NOT_INITIALIZED;
    LeaveCriticalSection(&stream_desc->cs);

    return hr;
}

static HRESULT WINAPI mediatype_handler_GetMajorType(IMFMediaTypeHandler *iface, GUID *type)
{
    FIXME("%p, %p.\n", iface, type);

    return E_NOTIMPL;
}

static const IMFMediaTypeHandlerVtbl mediatypehandlervtbl =
{
    mediatype_handler_QueryInterface,
    mediatype_handler_AddRef,
    mediatype_handler_Release,
    mediatype_handler_IsMediaTypeSupported,
    mediatype_handler_GetMediaTypeCount,
    mediatype_handler_GetMediaTypeByIndex,
    mediatype_handler_SetCurrentMediaType,
    mediatype_handler_GetCurrentMediaType,
    mediatype_handler_GetMajorType,
};

/***********************************************************************
 *      MFCreateStreamDescriptor (mfplat.@)
 */
HRESULT WINAPI MFCreateStreamDescriptor(DWORD identifier, DWORD count,
        IMFMediaType **types, IMFStreamDescriptor **descriptor)
{
    struct stream_desc *object;
    unsigned int i;

    TRACE("%d, %d, %p, %p.\n", identifier, count, types, descriptor);

    if (!count)
        return E_INVALIDARG;

    object = heap_alloc_zero(sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;

    init_attribute_object(&object->attributes, 0);
    object->IMFStreamDescriptor_iface.lpVtbl = &streamdescriptorvtbl;
    object->IMFMediaTypeHandler_iface.lpVtbl = &mediatypehandlervtbl;
    object->identifier = identifier;
    object->media_types = heap_alloc(count * sizeof(*object->media_types));
    if (!object->media_types)
    {
        heap_free(object);
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < count; ++i)
    {
        object->media_types[i] = types[i];
        if (object->media_types[i])
            IMFMediaType_AddRef(object->media_types[i]);
    }
    object->media_types_count = count;
    InitializeCriticalSection(&object->cs);

    *descriptor = &object->IMFStreamDescriptor_iface;

    return S_OK;
}

static HRESULT WINAPI presentation_descriptor_QueryInterface(IMFPresentationDescriptor *iface, REFIID riid, void **out)
{
    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), out);

    if (IsEqualIID(riid, &IID_IMFPresentationDescriptor) ||
            IsEqualIID(riid, &IID_IMFAttributes) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *out = iface;
        IMFPresentationDescriptor_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported %s.\n", debugstr_guid(riid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI presentation_descriptor_AddRef(IMFPresentationDescriptor *iface)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    ULONG refcount = InterlockedIncrement(&presentation_desc->attributes.ref);

    TRACE("%p, refcount %u.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI presentation_descriptor_Release(IMFPresentationDescriptor *iface)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    ULONG refcount = InterlockedDecrement(&presentation_desc->attributes.ref);
    unsigned int i;

    TRACE("%p, refcount %u.\n", iface, refcount);

    if (!refcount)
    {
        for (i = 0; i < presentation_desc->count; ++i)
        {
            if (presentation_desc->descriptors[i].descriptor)
                IMFStreamDescriptor_Release(presentation_desc->descriptors[i].descriptor);
        }
        DeleteCriticalSection(&presentation_desc->cs);
        heap_free(presentation_desc->descriptors);
        heap_free(presentation_desc);
    }

    return refcount;
}

static HRESULT WINAPI presentation_descriptor_GetItem(IMFPresentationDescriptor *iface, REFGUID key,
        PROPVARIANT *value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetItem(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_GetItemType(IMFPresentationDescriptor *iface, REFGUID key,
        MF_ATTRIBUTE_TYPE *type)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetItemType(&presentation_desc->attributes.IMFAttributes_iface, key, type);
}

static HRESULT WINAPI presentation_descriptor_CompareItem(IMFPresentationDescriptor *iface, REFGUID key,
        REFPROPVARIANT value, BOOL *result)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_CompareItem(&presentation_desc->attributes.IMFAttributes_iface, key, value, result);
}

static HRESULT WINAPI presentation_descriptor_Compare(IMFPresentationDescriptor *iface, IMFAttributes *attrs,
        MF_ATTRIBUTES_MATCH_TYPE type, BOOL *result)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_Compare(&presentation_desc->attributes.IMFAttributes_iface, attrs, type, result);
}

static HRESULT WINAPI presentation_descriptor_GetUINT32(IMFPresentationDescriptor *iface, REFGUID key, UINT32 *value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetUINT32(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_GetUINT64(IMFPresentationDescriptor *iface, REFGUID key, UINT64 *value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetUINT64(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_GetDouble(IMFPresentationDescriptor *iface, REFGUID key, double *value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetDouble(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_GetGUID(IMFPresentationDescriptor *iface, REFGUID key, GUID *value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetGUID(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_GetStringLength(IMFPresentationDescriptor *iface, REFGUID key,
        UINT32 *length)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetStringLength(&presentation_desc->attributes.IMFAttributes_iface, key, length);
}

static HRESULT WINAPI presentation_descriptor_GetString(IMFPresentationDescriptor *iface, REFGUID key, WCHAR *value,
        UINT32 size, UINT32 *length)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetString(&presentation_desc->attributes.IMFAttributes_iface, key, value, size, length);
}

static HRESULT WINAPI presentation_descriptor_GetAllocatedString(IMFPresentationDescriptor *iface, REFGUID key,
        WCHAR **value, UINT32 *length)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetAllocatedString(&presentation_desc->attributes.IMFAttributes_iface, key, value, length);
}

static HRESULT WINAPI presentation_descriptor_GetBlobSize(IMFPresentationDescriptor *iface, REFGUID key, UINT32 *size)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetBlobSize(&presentation_desc->attributes.IMFAttributes_iface, key, size);
}

static HRESULT WINAPI presentation_descriptor_GetBlob(IMFPresentationDescriptor *iface, REFGUID key, UINT8 *buf,
        UINT32 bufsize, UINT32 *blobsize)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetBlob(&presentation_desc->attributes.IMFAttributes_iface, key, buf, bufsize, blobsize);
}

static HRESULT WINAPI presentation_descriptor_GetAllocatedBlob(IMFPresentationDescriptor *iface, REFGUID key,
        UINT8 **buf, UINT32 *size)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetAllocatedBlob(&presentation_desc->attributes.IMFAttributes_iface, key, buf, size);
}

static HRESULT WINAPI presentation_descriptor_GetUnknown(IMFPresentationDescriptor *iface, REFGUID key,
        REFIID riid, void **ppv)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetUnknown(&presentation_desc->attributes.IMFAttributes_iface, key, riid, ppv);
}

static HRESULT WINAPI presentation_descriptor_SetItem(IMFPresentationDescriptor *iface, REFGUID key,
        REFPROPVARIANT value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_SetItem(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_DeleteItem(IMFPresentationDescriptor *iface, REFGUID key)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_DeleteItem(&presentation_desc->attributes.IMFAttributes_iface, key);
}

static HRESULT WINAPI presentation_descriptor_DeleteAllItems(IMFPresentationDescriptor *iface)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_DeleteAllItems(&presentation_desc->attributes.IMFAttributes_iface);
}

static HRESULT WINAPI presentation_descriptor_SetUINT32(IMFPresentationDescriptor *iface, REFGUID key, UINT32 value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_SetUINT32(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_SetUINT64(IMFPresentationDescriptor *iface, REFGUID key, UINT64 value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_SetUINT64(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_SetDouble(IMFPresentationDescriptor *iface, REFGUID key, double value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_SetDouble(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_SetGUID(IMFPresentationDescriptor *iface, REFGUID key, REFGUID value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_SetGUID(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_SetString(IMFPresentationDescriptor *iface, REFGUID key,
        const WCHAR *value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_SetString(&presentation_desc->attributes.IMFAttributes_iface, key, value);
}

static HRESULT WINAPI presentation_descriptor_SetBlob(IMFPresentationDescriptor *iface, REFGUID key, const UINT8 *buf,
        UINT32 size)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_SetBlob(&presentation_desc->attributes.IMFAttributes_iface, key, buf, size);
}

static HRESULT WINAPI presentation_descriptor_SetUnknown(IMFPresentationDescriptor *iface, REFGUID key,
        IUnknown *unknown)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_SetUnknown(&presentation_desc->attributes.IMFAttributes_iface, key, unknown);
}

static HRESULT WINAPI presentation_descriptor_LockStore(IMFPresentationDescriptor *iface)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_LockStore(&presentation_desc->attributes.IMFAttributes_iface);
}

static HRESULT WINAPI presentation_descriptor_UnlockStore(IMFPresentationDescriptor *iface)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_UnlockStore(&presentation_desc->attributes.IMFAttributes_iface);
}

static HRESULT WINAPI presentation_descriptor_GetCount(IMFPresentationDescriptor *iface, UINT32 *items)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetCount(&presentation_desc->attributes.IMFAttributes_iface, items);
}

static HRESULT WINAPI presentation_descriptor_GetItemByIndex(IMFPresentationDescriptor *iface, UINT32 index, GUID *key,
        PROPVARIANT *value)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    return IMFAttributes_GetItemByIndex(&presentation_desc->attributes.IMFAttributes_iface, index, key, value);
}

static HRESULT WINAPI presentation_descriptor_CopyAllItems(IMFPresentationDescriptor *iface, IMFAttributes *dest)
{
    FIXME("%p, %p.\n", iface, dest);

    return E_NOTIMPL;
}

static HRESULT WINAPI presentation_descriptor_GetStreamDescriptorCount(IMFPresentationDescriptor *iface, DWORD *count)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);

    TRACE("%p, %p.\n", iface, count);

    *count = presentation_desc->count;

    return S_OK;
}

static HRESULT WINAPI presentation_descriptor_GetStreamDescriptorByIndex(IMFPresentationDescriptor *iface, DWORD index,
        BOOL *selected, IMFStreamDescriptor **descriptor)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);

    TRACE("%p, %u, %p, %p.\n", iface, index, selected, descriptor);

    if (index >= presentation_desc->count)
        return E_INVALIDARG;

    EnterCriticalSection(&presentation_desc->cs);
    *selected = presentation_desc->descriptors[index].selected;
    LeaveCriticalSection(&presentation_desc->cs);

    *descriptor = presentation_desc->descriptors[index].descriptor;
    IMFStreamDescriptor_AddRef(*descriptor);

    return S_OK;
}

static HRESULT WINAPI presentation_descriptor_SelectStream(IMFPresentationDescriptor *iface, DWORD index)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);

    TRACE("%p, %u.\n", iface, index);

    if (index >= presentation_desc->count)
        return E_INVALIDARG;

    EnterCriticalSection(&presentation_desc->cs);
    presentation_desc->descriptors[index].selected = TRUE;
    LeaveCriticalSection(&presentation_desc->cs);

    return S_OK;
}

static HRESULT WINAPI presentation_descriptor_DeselectStream(IMFPresentationDescriptor *iface, DWORD index)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);

    TRACE("%p, %u.\n", iface, index);

    if (index >= presentation_desc->count)
        return E_INVALIDARG;

    EnterCriticalSection(&presentation_desc->cs);
    presentation_desc->descriptors[index].selected = FALSE;
    LeaveCriticalSection(&presentation_desc->cs);

    return S_OK;
}

static HRESULT WINAPI presentation_descriptor_Clone(IMFPresentationDescriptor *iface,
        IMFPresentationDescriptor **descriptor)
{
    struct presentation_desc *presentation_desc = impl_from_IMFPresentationDescriptor(iface);
    struct presentation_desc *object;
    unsigned int i;

    TRACE("%p, %p.\n", iface, descriptor);

    object = heap_alloc_zero(sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;

    presentation_descriptor_init(object, presentation_desc->count);

    EnterCriticalSection(&presentation_desc->cs);

    for (i = 0; i < presentation_desc->count; ++i)
    {
        object->descriptors[i] = presentation_desc->descriptors[i];
        IMFStreamDescriptor_AddRef(object->descriptors[i].descriptor);
    }
    /* FIXME: copy attributes */

    LeaveCriticalSection(&presentation_desc->cs);

    *descriptor = &object->IMFPresentationDescriptor_iface;

    return S_OK;
}

static const IMFPresentationDescriptorVtbl presentationdescriptorvtbl =
{
    presentation_descriptor_QueryInterface,
    presentation_descriptor_AddRef,
    presentation_descriptor_Release,
    presentation_descriptor_GetItem,
    presentation_descriptor_GetItemType,
    presentation_descriptor_CompareItem,
    presentation_descriptor_Compare,
    presentation_descriptor_GetUINT32,
    presentation_descriptor_GetUINT64,
    presentation_descriptor_GetDouble,
    presentation_descriptor_GetGUID,
    presentation_descriptor_GetStringLength,
    presentation_descriptor_GetString,
    presentation_descriptor_GetAllocatedString,
    presentation_descriptor_GetBlobSize,
    presentation_descriptor_GetBlob,
    presentation_descriptor_GetAllocatedBlob,
    presentation_descriptor_GetUnknown,
    presentation_descriptor_SetItem,
    presentation_descriptor_DeleteItem,
    presentation_descriptor_DeleteAllItems,
    presentation_descriptor_SetUINT32,
    presentation_descriptor_SetUINT64,
    presentation_descriptor_SetDouble,
    presentation_descriptor_SetGUID,
    presentation_descriptor_SetString,
    presentation_descriptor_SetBlob,
    presentation_descriptor_SetUnknown,
    presentation_descriptor_LockStore,
    presentation_descriptor_UnlockStore,
    presentation_descriptor_GetCount,
    presentation_descriptor_GetItemByIndex,
    presentation_descriptor_CopyAllItems,
    presentation_descriptor_GetStreamDescriptorCount,
    presentation_descriptor_GetStreamDescriptorByIndex,
    presentation_descriptor_SelectStream,
    presentation_descriptor_DeselectStream,
    presentation_descriptor_Clone,
};

static HRESULT presentation_descriptor_init(struct presentation_desc *object, DWORD count)
{
    init_attribute_object(&object->attributes, 0);
    object->IMFPresentationDescriptor_iface.lpVtbl = &presentationdescriptorvtbl;
    object->descriptors = heap_alloc_zero(count * sizeof(*object->descriptors));
    if (!object->descriptors)
    {
        heap_free(object);
        return E_OUTOFMEMORY;
    }
    object->count = count;
    InitializeCriticalSection(&object->cs);

    return S_OK;
}

/***********************************************************************
 *      MFCreatePresentationDescriptor (mfplat.@)
 */
HRESULT WINAPI MFCreatePresentationDescriptor(DWORD count, IMFStreamDescriptor **descriptors,
        IMFPresentationDescriptor **out)
{
    struct presentation_desc *object;
    unsigned int i;
    HRESULT hr;

    TRACE("%u, %p, %p.\n", count, descriptors, out);

    if (!count)
        return E_INVALIDARG;

    for (i = 0; i < count; ++i)
    {
        if (!descriptors[i])
            return E_INVALIDARG;
    }

    object = heap_alloc_zero(sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;

    if (FAILED(hr = presentation_descriptor_init(object, count)))
    {
        heap_free(object);
        return hr;
    }

    for (i = 0; i < count; ++i)
    {
        object->descriptors[i].descriptor = descriptors[i];
        IMFStreamDescriptor_AddRef(object->descriptors[i].descriptor);
    }

    *out = &object->IMFPresentationDescriptor_iface;

    return S_OK;
}
