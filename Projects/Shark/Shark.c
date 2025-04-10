/*
*
* Copyright (c) 2015 - 2021 by blindtiger. All rights reserved.
*
* The contents of this file are subject to the Mozilla Public License Version
* 2.0 (the "License")); you may not use this file except in compliance with
* the License. You may obtain a copy of the License at
* http://www.mozilla.org/MPL/
*
* Software distributed under the License is distributed on an "AS IS" basis,
* WITHOUT WARRANTY OF ANY KIND, either express or implied. SEe the License
* for the specific language governing rights and limitations under the
* License.
*
* The Initial Developer of the Original Code is blindtiger.
*
*/

#include <defs.h>
#include <devicedefs.h>

#include "Shark.h"

#include "Except.h"
#include "Guard.h"
#include "Reload.h"
#include "PatchGuard.h"
#include "Space.h"

#pragma section( ".block", read, write, execute )

__declspec(allocate(".block")) RTB RtBlock = { 0 };
__declspec(allocate(".block")) PGBLOCK PgBlock = { 0 };

status
NTAPI
KernelFastCall(
    __in ptr Reserve,
    __in u32 Operation
)
{
    status Status = STATUS_SUCCESS;

#ifdef DEBUG
    vDbgPrint(
        "[Shark] KernelFastCall\n");
#endif // DEBUG

    switch (Operation) {
    case 0: {
        break;
    }

    case 1: {
        break;
    }

    case 2: {
        break;
    }

    default: {
        break;
    }
    }

    return Status;
}

status
NTAPI
ReloadSelf(
    __in PRTB RtBlock,
    __in u32 Operation
)
{
    status Status = STATUS_SUCCESS;
    PKLDR_DATA_TABLE_ENTRY DataTableEntry = NULL;
    PGNORMAL_ROUTINE GsInitialize = NULL;
    PGSUPPORT_ROUTINE KernelEntry = NULL;

    DataTableEntry = LdrLoad(
        RtBlock->Self->SupImage.pvImage,
        KernelString,
        LDRP_SYSTEM_MAPPED | LDRP_REDIRECTED);

    if (NULL != DataTableEntry) {
#ifdef DEBUG
        vDbgPrint(
            ".reload /i %wZ=%p < %p - %08x >\n",
            &DataTableEntry->BaseDllName,
            DataTableEntry->DllBase,
            DataTableEntry->DllBase,
            DataTableEntry->SizeOfImage);
#endif // DEBUG

        GsInitialize = DataTableEntry->EntryPoint;

        if (NULL != GsInitialize) {
            GsInitialize();

            KernelEntry =
                LdrGetSymbol(
                    DataTableEntry->DllBase,
                    "mpsi_load",
                    0);

            if (NULL != KernelEntry) {
                Status = KernelEntry(
                    (ptr)DataTableEntry,
                    (ptr)(u)Operation,
                    NULL,
                    NULL);
            }
            else {
                __free(DataTableEntry);

                Status = STATUS_UNSUCCESSFUL;
            }
        }
        else {
            __free(DataTableEntry);

            Status = STATUS_UNSUCCESSFUL;
        }
    }
    else {
        Status = STATUS_UNSUCCESSFUL;

#ifdef DEBUG
        vDbgPrint(
            "[Shark] reload failed\n");
#endif // DEBUG
    }

    return Status;
}

ULONG __cdecl
MyDbgPrint(
	__in PCSTR Format,
	...
)
{
	va_list arglist;
	va_start(arglist, Format);

	const ULONG ret = vDbgPrintEx(77, 0, Format, arglist);
	
	va_end(arglist);
	
	return ret;
}

static void WPON(KIRQL irql)
{
	ULONG_PTR cr0 = __readcr0();
	cr0 |= 0x10000;
	__writecr0(cr0);
	_enable();
	KeLowerIrql(irql);
}

static KIRQL WPOFF()
{
	KIRQL irql = KeRaiseIrqlToDpcLevel();
	ULONG_PTR cr0 = __readcr0();
	cr0 &= 0xfffffffffffeffff;
	_disable();
	__writecr0(cr0);
	return irql;
}

static
BOOLEAN 
NTAPI MarkDriverLoadedFlag(PDRIVER_OBJECT lpDriverObject)
{
	if (!lpDriverObject)
	{
		return FALSE;
	}

	PLIST_ENTRY lpEntry = NULL, lpOrigin = NULL;
	lpOrigin = (PLIST_ENTRY)lpDriverObject->DriverSection;
	lpEntry = lpOrigin;
	if (!lpEntry)
	{
		return FALSE;
	}

	WCHAR wszBeepDriverName[] = {L'b', L'e', L'e', L'p', L'.', L's', L'y', L's', L'\0'};

	UNICODE_STRING uniDrvName = { 0 };
	RtlInitUnicodeString(&uniDrvName, wszBeepDriverName);

	lpEntry = lpEntry->Flink;
	while (lpEntry && lpEntry != lpOrigin)
	{
		PLDR_DATA_TABLE_ENTRY TableEntry = CONTAINING_RECORD(lpEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
		if (TableEntry->SizeOfImage && TableEntry->DllBase && MmIsAddressValid(TableEntry) && MmIsAddressValid(TableEntry->BaseDllName.Buffer))
		{
			if (RtlEqualUnicodeString(&uniDrvName, &TableEntry->BaseDllName, TRUE))
			{
#ifdef DEBUG
				vDbgPrint("[SHARK] mark address: 0x%llX\n", TableEntry->DllBase);
#endif // DEBUG

				KIRQL irql = WPOFF();
				
				ULONG ulPatchValue = 0x00004B53; /** 'SK' */
				RtlCopyMemory(TableEntry->DllBase, &ulPatchValue, sizeof(ULONG));
				
				WPON(irql);

				break;
			}
		}
		lpEntry = lpEntry->Flink;
	}

	return TRUE;
}

status
NTAPI
KernelEntry(
    __in ptr Self,
    __in u32 Operation,
    __in ptr LoaderDrvBase,
    __in ptr Nothing
)
{
    RtBlock.PgBlock = &PgBlock;
    PgBlock.RtBlock = &RtBlock;

    RtBlock.Self = Self;
    RtBlock.Operation = Operation;

    InitializeGpBlock(&RtBlock);

    if (CmdReload ==
        (RtBlock.Operation & CmdReload)) {
        ReloadSelf(&RtBlock, Operation & ~CmdReload);
    }
    else {
        InitializeSpace(&RtBlock);

        if (CmdPgClear ==
            (RtBlock.Operation & CmdPgClear)) {
            PgBlock.IsDebug = 1;

#ifndef _WIN64
            InitializeExcept(&RtBlock);
#else                             
            PgClear(&PgBlock);
            InitializeExcept(&RtBlock);
#endif // !_WIN64

            __try {
                *(volatile u8ptr)NULL;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
#ifdef DEBUG
                vDbgPrint(
                    "[SHARK] < %p > test exception code\n",
                    GetExceptionCode());
#endif // DEBUG    
            }
        }

        if (CmdVmxOn ==
            (RtBlock.Operation & CmdVmxOn)) {
            // VmxStartAllProcessors(&RtBlock.CpuControlBlock);
        }

#ifdef DEBUG
        vDbgPrint(
            "[SHARK] < %p > shark load success\n",
            RtBlock.Self);
#endif // DEBUG
    }

	if (LoaderDrvBase)
	{
		BOOLEAN bRet = MarkDriverLoadedFlag(LoaderDrvBase);
#ifdef DEBUG
		vDbgPrint("[SHARK] mark driver loaded flag=%d\n", bRet);
#endif // DEBUG
	}

#ifdef DEBUG
	vDbgPrint("[SHARK] load success\n");
#endif // DEBUG

    return STATUS_SUCCESS;
}
