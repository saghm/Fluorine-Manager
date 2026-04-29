#pragma once

#include "skseLogRedirectorBase.h"

class SkseLogRedirector : public SkseLogRedirectorBase
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.sysdmp.SKSELogRedirector")

public:
  QString name() const override;
  QString localizedName() const override;
  QString description() const override;

protected:
  QString destFolderName() const override;
};
