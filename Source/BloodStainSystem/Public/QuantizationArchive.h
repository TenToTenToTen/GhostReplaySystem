// QuantizationArchive.h
#pragma once

#include "BloodStainFileOptions.h"
#include "Serialization/ArchiveProxy.h"
#include "TransformQuantizer.h"

class BLOODSTAINSYSTEM_API FQuantizationArchive : public FArchiveProxy
{
public:
    // 생성자에서 옵션 기반으로 Quantizer 하나를 만듭니다.
    FQuantizationArchive(FArchive& InInnerArchive, const FQuantizationOption& InOptions)
        : FArchiveProxy(InInnerArchive), Options(InOptions)
    {
        this->SetIsSaving(true);
    }
    using FArchiveProxy::operator<<;

    virtual FArchive& operator<<(FTransform& Value)
    {
        switch (Options.Method)
        {
        case ETransformQuantizationMethod::Standard_High:
            {
                FQuantizedTransform_High Quantized(Value);
                InnerArchive << Quantized;
                break;
            }
        case ETransformQuantizationMethod::Standard_Compact:
            {
                FQuantizedTransform_Compact Quantized(Value);
                InnerArchive << Quantized;
                break;
            }
        case ETransformQuantizationMethod::None:
        default:
            InnerArchive << Value;
        }
        return *this;
    }
    
    TUniquePtr<ITransformQuantizer> Quantizer;
private:
    const FQuantizationOption& Options;
};


class BLOODSTAINSYSTEM_API FDequantizationArchive : public FArchiveProxy
{
public:
    FDequantizationArchive(FArchive& InInnerArchive, const FQuantizationOption& InOptions)
        : FArchiveProxy(InInnerArchive), Options(InOptions)
    {
        this->SetIsLoading(true);
    }
    using FArchiveProxy::operator<<;

    FArchive& operator<<(FTransform& Value)
    {
        switch (Options.Method)
        {
        case ETransformQuantizationMethod::Standard_High:
            {
                FQuantizedTransform_High Quantized;
                InnerArchive << Quantized;
                Value = Quantized.ToTransform();
                break;
            }
        case ETransformQuantizationMethod::Standard_Compact:
            {
                FQuantizedTransform_Compact Quantized;
                InnerArchive << Quantized;
                Value = Quantized.ToTransform();
                break;
            }
        case ETransformQuantizationMethod::None:
        default:
            InnerArchive << Value;
            break;
        }
        return *this;
    }
    
    TUniquePtr<ITransformQuantizer> Quantizer;
private:
    const FQuantizationOption& Options;
};