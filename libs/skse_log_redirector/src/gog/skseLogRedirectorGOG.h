#pragma once

#include "skseLogRedirectorBase.h"

class SkseLogRedirectorGOG : public SkseLogRedirectorBase
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.sysdmp.SKSELogRedirectorGOG")

public:
  QString name() const override;
  QString localizedName() const override;
  QString description() const override;

protected:
  QString destFolderName() const override;
};
