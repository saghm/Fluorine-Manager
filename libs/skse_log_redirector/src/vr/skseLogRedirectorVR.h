#pragma once

#include "skseLogRedirectorBase.h"

class SkseLogRedirectorVR : public SkseLogRedirectorBase
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.sysdmp.SKSELogRedirectorVR")

public:
  QString name() const override;
  QString localizedName() const override;
  QString description() const override;

protected:
  QString destFolderName() const override;
};
