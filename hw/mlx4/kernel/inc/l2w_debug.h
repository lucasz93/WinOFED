#pragma once

VOID
WriteEventLogEntryStr(
	__in PVOID	pi_pIoObject,
	__in ULONG	pi_ErrorCode,
	__in ULONG	pi_UniqueErrorCode,
	__in ULONG	pi_FinalStatus,
	__in PWCHAR	pi_InsertionStr,
	__in ULONG	pi_nDataItems,
	...
	);

VOID
WriteEventLogEntryData(
	PVOID	pi_pIoObject,
	ULONG	pi_ErrorCode,
	ULONG	pi_UniqueErrorCode,
	ULONG	pi_FinalStatus,
	ULONG	pi_nDataItems,
	...
	);

struct mlx4_dev;

void
mlx4_err(
	__in struct mlx4_dev *	mdev,
	__in char*				format,
	...
	);
void

mlx4_warn(
	__in struct mlx4_dev *	mdev,
	__in char*				format,
	...
	);

void
mlx4_dbg(
	__in struct mlx4_dev *	mdev,
	__in char*				format,
	...
	);

VOID
dev_err(
	__in struct mlx4_dev **	mdev,
	__in char*				format,
	...
	);

VOID
dev_info(
	__in struct mlx4_dev **	p_mdev,
	__in char*				format,
	...
	);

#define mlx4_info	mlx4_dbg
#define dev_warn	dev_err

