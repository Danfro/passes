// **************************************************************************
// class PassesModel
// 02.07.2021
// Model for handling locally stored passes managed by this app
// **************************************************************************
// MIT License
// Copyright © 2021 Patrick Fial
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// **************************************************************************
// includes
// **************************************************************************

#include "passesmodel.h"
#include <QStandardPaths>
#include <QDebug>

namespace C {
#include <libintl.h>
}

namespace passes
{
   // **************************************************************************
   // class PassesModel
   // **************************************************************************

   PassesModel* PassesModel::instance = nullptr;

   PassesModel::PassesModel(QObject *parent)
      : QAbstractListModel(parent), storageReady(false), countExpired(0)
   {
      instance = this;
   }

   // **************************************************************************
   // init
   // **************************************************************************

   QString PassesModel::init()
   {
      QString dataPath = getDataPath();

      if (!dataPath.size())
         return C::gettext("Failed to determine writable app data storage location");

      qDebug() << "DATA PATH: " << dataPath;

      QDir dir(dataPath + "/passes");

      if (!dir.exists())
      {
         bool res = dir.mkpath(dataPath + "/passes");

         if (!res)
            return QString(C::gettext("App data storage location inaccessible"))
                  + " (" + QString(C::gettext("can't create subfolder:"))
                  + " " + dir.absolutePath() + ")";
      }

      passesDir.setPath(dir.path());
      passesDir.setPath("/home/phablet/passes");
      passesDir.setSorting(QDir::Time);
      storageReady = true;

      return "";
   }

   // **************************************************************************
   // data
   // **************************************************************************

   QVariant PassesModel::data(const QModelIndex& index, int role) const
   {
      if (role == Qt::DisplayRole || role == PassRole)
         return static_cast<QVariant>(*(mItems[index.row()]));

      return QVariant();
   }

   // **************************************************************************
   // rowCount
   // **************************************************************************

   int PassesModel::rowCount(const QModelIndex& /*parent*/) const
   {
      return mItems.size();
   }

   // **************************************************************************
   // roleNames
   // **************************************************************************

   QHash<int, QByteArray> PassesModel::roleNames() const
   {
       QHash<int, QByteArray> roles;
       roles[PassRole] = "pass";
       return roles;
   }

   // **************************************************************************
   // getDataPath
   // **************************************************************************

   QString PassesModel::getDataPath() const
   {
      return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
   }

   // **************************************************************************
   // reload
   // **************************************************************************

   void PassesModel::reload()
   {
      if (!storageReady || !passesDir.path().size())
      {
         qDebug() << "Storage directory not initialized";
         return;
      }

      beginResetModel();

      for (auto p : mItems)
         delete p;

      mItems.clear();
      mItemMap.clear();
      countExpired = 0;

      QVariantList failed;

      for (const QFileInfo& info : passesDir.entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot | QDir::Readable))
      {
         if (info.fileName().startsWith(".") || !info.fileName().endsWith(".pkpass"))
            continue;

         auto passResult = pkpass.openPass(info.absoluteFilePath());

         if (!passResult.err.isEmpty())
         {
            qDebug() << "Pass open failed: " << passResult.err;

            QVariantMap failedPass;
            failedPass["filePath"] = passResult.pass->filePath;
            failedPass["error"] = passResult.err;
            failed.append(failedPass);

            delete passResult.pass;
         }
         else if (passResult.pass->standard.expired)
         {
            delete passResult.pass;
            countExpired++;
         }
         else
         {
            mItemMap[passResult.pass->id] = passResult.pass;
            mItems.push_back(passResult.pass);
         }
      }

      std::sort(mItems.begin(), mItems.end(), passSorter);

      endResetModel();
      emit countExpiredChanged();
      emit countChanged();

      if (failed.length())
      {
         qDebug() << failed.length() << " passed failed to open";
         emit failedPasses(failed);
      }
   }

   // **************************************************************************
   // openPasses
   // **************************************************************************

   void PassesModel::showExpired()
   {
      size_t oldCount = mItems.size();
      QVariantList failed;

      for (const QFileInfo& info : passesDir.entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot | QDir::Readable))
      {
         if (info.fileName().startsWith(".") || !info.fileName().endsWith(".pkpass"))
            continue;

         if (isOpen(info.absoluteFilePath()))
         {
            qDebug() << "Skipping open of already opened pass " << info.absoluteFilePath();
            continue;
         }

         auto passResult = pkpass.openPass(info.absoluteFilePath());

         if (!passResult.err.isEmpty())
         {
            qDebug() << "Pass open failed: " << passResult.err;

            QVariantMap failedPass;
            failedPass["filePath"] = passResult.pass->filePath;
            failedPass["error"] = passResult.err;
            failed.append(failedPass);
         }
         else if (!passResult.pass->standard.expired)
         {
            delete passResult.pass;
         }
         else
         {
            mItemMap[passResult.pass->id] = passResult.pass;
            mItems.push_back(passResult.pass);
         }
      }

      if (failed.length())
      {
         qDebug() << failed.length() << " passed failed to open";
         emit failedPasses(failed);
      }

      if (mItems.size() != oldCount)
      {
         std::sort(mItems.begin(), mItems.end(), passSorter);
         beginResetModel();
         endResetModel();
         emit countChanged();
      }
   }

   // **************************************************************************
   // hideExpired
   // **************************************************************************

   void PassesModel::hideExpired()
   {
      size_t oldCount = mItems.size();

      for (auto it = mItems.begin() ; it != mItems.end(); )
      {
         if ((*it)->standard.expired)
         {
            mItemMap.erase((*it)->id);
            delete *it;
            it = mItems.erase(it);
         }
         else
         {
            it++;
         }
      }

      if (mItems.size() != oldCount)
      {
         beginResetModel();
         endResetModel();
         emit countChanged();
      }
   }

   // **************************************************************************
   // isOpen
   // **************************************************************************

   bool PassesModel::isOpen(const QString& filePath)
   {
      auto it = std::find_if(mItems.begin(), mItems.end(), [&filePath](Pass* pass){ return pass->filePath == filePath; });

      return it != mItems.end();
   }

   // **************************************************************************
   // importPass
   // **************************************************************************

   QString PassesModel::importPass(const QString& filePath, bool expiredShown)
   {
      QString fp = filePath;

      if (fp.startsWith("file://"))
         fp.remove("file://");

      if (!storageReady)
         return C::gettext("Storage directory inaccessible, cannot import pass");

      if (!QFile::exists(fp))
         return C::gettext("File path of the pass seems to be invalid");

      QFileInfo info(fp);
      QString targetPath = passesDir.path() + "/" + info.fileName();

      qDebug() << "Importing pass: " << fp << " To: " << targetPath;

      if (QFile::exists(targetPath))
         return C::gettext("Same pass has already been imported");

      QFile sourceFile(fp);
      bool res = sourceFile.copy(targetPath);

      if (!res)
         return QString(C::gettext("Failed to import pass into storage directory ")) + " (" + sourceFile.errorString() + ")";

      info.setFile(targetPath);

      auto passResult = pkpass.openPass(info.absoluteFilePath());
      auto it = std::find_if(mItems.begin(), mItems.end(), [&passResult](Pass* pass) { return pass->id == passResult.pass->id; });

      if (!passResult.err.isEmpty())
      {
         qDebug() << "Pass open failed: " << passResult.err;

         QFile::remove(targetPath);

         delete passResult.pass;
         return QString(C::gettext("Failed to open pass")) + " (" + passResult.err + ")";
      }
      else if (it != mItems.end())
      {
         QFile::remove(targetPath);

         delete passResult.pass;
         return C::gettext("Pass with the same barcode has already been imported");
      }
      else if (passResult.pass->standard.expired && !expiredShown)
      {
         countExpired++;
         delete passResult.pass;
      }
      else
      {
         if (passResult.pass->standard.expired)
            countExpired++;

         mItemMap[passResult.pass->id] = passResult.pass;
         mItems.push_back(passResult.pass);
      }

      std::sort(mItems.begin(), mItems.end(), passSorter);
      beginResetModel();
      endResetModel();
      emit countChanged();
      emit countExpiredChanged();

      return "";
   }

   // **************************************************************************
   // deletePass
   // **************************************************************************

   QString PassesModel::deletePass(QString filePath, bool failedPass)
   {
      qDebug() << "Deleting pass " << filePath;

      if (failedPass)
      {
         if (!QFile::exists(filePath))
            return C::gettext("Failed to delete pass (pass unknown)");

         QFile passFile(filePath);
         bool res = passFile.remove();

         if (!res)
            return QString(C::gettext("Failed to delete pass from storage directory (%1)")).arg(passFile.errorString());

         return "";
      }

      auto it = std::find_if(mItems.begin(), mItems.end(), [&filePath](Pass* pass) { return pass->filePath == filePath; });

      if (it == mItems.end() || !QFile::exists(filePath) || !mItemMap.count((*it)->id))
         return C::gettext("Failed to delete pass (pass unknown)");

      auto pass = *it;
      QFile passFile(filePath);
      bool res = passFile.remove();

      if (!res)
         return QString(C::gettext("Failed to delete pass from storage directory (%1)")).arg(passFile.errorString());

      if (pass->standard.expired)
      {
         countExpired--;
         emit countExpiredChanged();
      }

      beginResetModel();

      mItemMap.erase(pass->id);
      mItems.erase(it);
      delete pass;

      endResetModel();
      emit countChanged();
      return "";
   }
} // namespace passes
