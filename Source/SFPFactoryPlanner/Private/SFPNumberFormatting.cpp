#include "SFPNumberFormatting.h"

#include "SFPLocalization.h"

FString FSFPNumberFormatting::Decimal(const double Value, const int32 MaxFractionalDigits)
{
	if (!FMath::IsFinite(Value))
	{
		return TEXT("—");
	}

	const int32 Digits = FMath::Clamp(MaxFractionalDigits, 0, 3);
	const double ZeroThreshold = Digits == 0
		? 0.5
		: Digits == 1
			? 0.05
			: Digits == 2
				? 0.005
				: 0.0005;
	const double DisplayValue = FMath::Abs(Value) < ZeroThreshold ? 0.0 : Value;

	FString Raw;
	switch (Digits)
	{
	case 0:
		Raw = FString::Printf(TEXT("%.0f"), DisplayValue);
		break;
	case 1:
		Raw = FString::Printf(TEXT("%.1f"), DisplayValue);
		break;
	case 2:
		Raw = FString::Printf(TEXT("%.2f"), DisplayValue);
		break;
	default:
		Raw = FString::Printf(TEXT("%.3f"), DisplayValue);
		break;
	}

	FString IntegerPart;
	FString FractionalPart;
	if (!Raw.Split(TEXT("."), &IntegerPart, &FractionalPart))
	{
		IntegerPart = Raw;
	}
	while (FractionalPart.EndsWith(TEXT("0")))
	{
		FractionalPart.LeftChopInline(1);
	}

	const bool bNegative = IntegerPart.RemoveFromStart(TEXT("-"));
	const TCHAR GroupSeparator = SFPLocalization::IsGerman() ? TEXT('.') : TEXT(',');
	const TCHAR DecimalSeparator = SFPLocalization::IsGerman() ? TEXT(',') : TEXT('.');
	FString GroupedInteger;
	GroupedInteger.Reserve(IntegerPart.Len() + IntegerPart.Len() / 3 + 1);
	for (int32 Index = 0; Index < IntegerPart.Len(); ++Index)
	{
		if (Index > 0 && (IntegerPart.Len() - Index) % 3 == 0)
		{
			GroupedInteger.AppendChar(GroupSeparator);
		}
		GroupedInteger.AppendChar(IntegerPart[Index]);
	}
	if (bNegative)
	{
		GroupedInteger.InsertAt(0, TEXT('-'));
	}

	return FractionalPart.IsEmpty()
		? GroupedInteger
		: GroupedInteger + FString::Chr(DecimalSeparator) + FractionalPart;
}
