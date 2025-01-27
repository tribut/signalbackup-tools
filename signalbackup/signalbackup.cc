/*
    Copyright (C) 2019  Selwin van Dijk

    This file is part of signalbackup-tools.

    signalbackup-tools is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    signalbackup-tools is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with signalbackup-tools.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "signalbackup.ih"

SignalBackup::SignalBackup(std::string const &filename, std::string const &passphrase, bool issource)
  :
  d_database(":memory:"),
  d_fd(new FileDecryptor(filename, passphrase, issource)),
  d_passphrase(passphrase),
  d_ok(false)
{
  if (!d_fd->ok())
  {
    std::cout << "Failed to create filedecrypter" << std::endl;
    return;
  }

  uint64_t totalsize = d_fd->total();

  std::cout << "Reading backup file..." << std::endl;
  std::unique_ptr<BackupFrame> frame(nullptr);

  d_database.exec("BEGIN TRANSACTION");

  while ((frame = d_fd->getFrame())) // deal with bad mac??
  {

    if (d_fd->badMac())
    {
      std::cout << std::endl << "WARNING: Bad MAC in frame, trying to print frame info:" << std::endl;
      frame->printInfo();

      if (frame->frameType() == BackupFrame::FRAMETYPE::ATTACHMENT)
      {
        std::unique_ptr<AttachmentFrame> a = std::make_unique<AttachmentFrame>(*reinterpret_cast<AttachmentFrame *>(frame.get()));
        //std::unique_ptr<AttachmentFrame> a(reinterpret_cast<AttachmentFrame *>(frame.release()));

        uint32_t rowid = a->rowId();
        uint64_t uniqueid = a->attachmentId();

        d_badattachments.emplace_back(std::make_pair(rowid, uniqueid));

        std::cout << "Frame is attachment, it belongs to entry in the 'part' table of the database:" << std::endl;
        //std::vector<std::vector<std::pair<std::string, std::any>>> results;
        SqliteDB::QueryResults results;
        std::string query = "SELECT * FROM part WHERE _id = " + bepaald::toString(rowid) + " AND unique_id = " + bepaald::toString(uniqueid);
        long long int mid = -1;
        d_database.exec(query, &results);
        for (uint i = 0; i < results.rows(); ++i)
        {
          for (uint j = 0; j < results.columns(); ++j)
          {
            std::cout << " - " << results.header(j) << " : ";
            if (results.valueHasType<std::nullptr_t>(i, j))
              std::cout << "(NULL)" << std::endl;
            else if (results.valueHasType<std::string>(i, j))
              std::cout << results.getValueAs<std::string>(i, j) << std::endl;
            else if (results.valueHasType<double>(i, j))
              std::cout << results.getValueAs<double>(i, j) << std::endl;
            else if (results.valueHasType<long long int>(i, j))
            {
              if (results.header(j) == "mid")
                mid = results.getValueAs<long long int>(i, j);
              std::cout << results.getValueAs<long long int>(i, j) << std::endl;
            }
            else if (results.valueHasType<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j))
              std::cout << bepaald::bytesToHexString(results.getValueAs<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j)) << std::endl;
           else
              std::cout << "(unhandled result type)" << std::endl;
          }
        }

        std::cout << std::endl << "Which belongs to entry in 'mms' table:" << std::endl;
        query = "SELECT * FROM mms WHERE _id = " + bepaald::toString(mid);
        d_database.exec(query, &results);

        for (uint i = 0; i < results.rows(); ++i)
          for (uint j = 0; j < results.columns(); ++j)
          {
            std::cout << " - " << results.header(j) << " : ";
            if (results.valueHasType<std::nullptr_t>(i, j))
              std::cout << "(NULL)" << std::endl;
            else if (results.valueHasType<std::string>(i, j))
              std::cout << results.getValueAs<std::string>(i, j) << std::endl;
            else if (results.valueHasType<double>(i, j))
              std::cout << results.getValueAs<double>(i, j) << std::endl;
            else if (results.valueHasType<long long int>(i, j))
            {
              if (results.header(j) == "date" ||
                  results.header(j) == "date_received")
              {
                long long int datum = results.getValueAs<long long int>(i, j);
                std::time_t epoch = datum / 1000;
                std::cout << std::put_time(std::localtime(&epoch), "%F %T %z") << " (" << results.getValueAs<long long int>(i, j) << ")" << std::endl;
              }
              else
                std::cout << results.getValueAs<long long int>(i, j) << std::endl;
            }
            else if (results.valueHasType<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j))
              std::cout << bepaald::bytesToHexString(results.getValueAs<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j)) << std::endl;
            else
              std::cout << "(unhandled result type)" << std::endl;
          }

        std::string afilename = "attachment_" + bepaald::toString(mid) + ".bin";
        std::cout << "Trying to dump decoded attachment to file '" << afilename << "'" << std::endl;

        std::ofstream bindump(afilename, std::ios::binary);
        bindump.write(reinterpret_cast<char *>(a->attachmentData()), a->attachmentSize());

      }
      else
      {
        std::cout << "Error: Bad MAC in frame other than AttachmentFrame. Not sure what to do..." << std::endl;
        frame->printInfo();
      }
    } // if (d_fd->badMac())

    std::cout << "\33[2K\rFRAME " << frame->frameNumber() << " ("
              << std::fixed << std::setprecision(1) << std::setw(5) << std::setfill('0')
              << (static_cast<float>(d_fd->curFilePos()) / totalsize) * 100 << "%)" << std::defaultfloat
              << "... " << std::flush;

    if (frame->frameType() == BackupFrame::FRAMETYPE::HEADER)
    {
      d_headerframe.reset(reinterpret_cast<HeaderFrame *>(frame.release()));
      //d_headerframe->printInfo();
    }
    else if (frame->frameType() == BackupFrame::FRAMETYPE::DATABASEVERSION)
    {
      d_databaseversionframe.reset(reinterpret_cast<DatabaseVersionFrame *>(frame.release()));
      //d_databaseversionframe->printInfo();
    }
    else if (frame->frameType() == BackupFrame::FRAMETYPE::SQLSTATEMENT)
    {
      SqlStatementFrame *s = reinterpret_cast<SqlStatementFrame *>(frame.get());
      if (s->statement().find("CREATE TABLE sqlite_") == std::string::npos)
        d_database.exec(s->bindStatement(), s->parameters());
    }
    else if (frame->frameType() == BackupFrame::FRAMETYPE::ATTACHMENT)
    {
      AttachmentFrame *a = reinterpret_cast<AttachmentFrame *>(frame.release());
      d_attachments.emplace(std::make_pair(a->rowId(), a->attachmentId()), a);
    }
    else if (frame->frameType() == BackupFrame::FRAMETYPE::AVATAR)
    {
      AvatarFrame *a = reinterpret_cast<AvatarFrame *>(frame.release());
      d_avatars.emplace(std::string(a->name()), a);
    }
    else if (frame->frameType() == BackupFrame::FRAMETYPE::SHAREDPREFERENCE)
      d_sharedpreferenceframes.emplace_back(reinterpret_cast<SharedPrefFrame *>(frame.release()));
    else if (frame->frameType() == BackupFrame::FRAMETYPE::STICKER)
    {
      StickerFrame *s = reinterpret_cast<StickerFrame *>(frame.release());
      d_stickers.emplace(s->rowId(), s);
    }
    else if (frame->frameType() == BackupFrame::FRAMETYPE::END)
      d_endframe.reset(reinterpret_cast<EndFrame *>(frame.release()));
  }

  d_database.exec("COMMIT");

  std::cout << "done!" << std::endl;

  d_ok = true;
}

SignalBackup::SignalBackup(std::string const &inputdir)
  :
  d_database(":memory:"),
  d_fe(),
  d_ok(false)
{

  std::cout << "Opening from dir!" << std::endl;

  SqliteDB database(inputdir + "/database.sqlite");
  if (!SqliteDB::copyDb(database, d_database))
    return;

  if (!setFrameFromFile(&d_headerframe, inputdir + "/Header.sbf"))
    return;

  //d_headerframe->printInfo();

  if (!setFrameFromFile(&d_databaseversionframe, inputdir + "/DatabaseVersion.sbf"))
    return;

  //d_databaseversionframe->printInfo();

  int idx = 0;
  while (true)
  {
    d_sharedpreferenceframes.resize(d_sharedpreferenceframes.size() + 1);
    if (!setFrameFromFile(&d_sharedpreferenceframes.back(), inputdir + "/SharedPreference_" + bepaald::toString(idx) + ".sbf", true))
    {
      d_sharedpreferenceframes.pop_back();
      break;
    }
    //d_sharedpreferenceframes.back()->printInfo();
    ++idx;
  }

  if (!setFrameFromFile(&d_endframe, inputdir + "/End.sbf"))
    return;

  //d_endframe->printInfo();

  // avatars
  std::error_code ec;
  std::filesystem::directory_iterator dirit(inputdir, ec);
  if (ec)
  {
    std::cout << "Error iterating directory `" << inputdir << "' : " << ec.message() << std::endl;
    return;
  }
  for (auto const &avatar : dirit)
  {
    if (avatar.path().filename().string().substr(0, STRLEN("Avatar_")) != "Avatar_" || avatar.path().extension() != ".sbf")
      continue;

    std::filesystem::path avatarframe = avatar.path();
    std::filesystem::path avatarbin = avatar.path();
    avatarbin.replace_extension(".bin");

    std::unique_ptr<AvatarFrame> temp;
    if (!setFrameFromFile(&temp, avatarframe.string()))
      return;
    temp->setAttachmentData(avatarbin.string());

    //temp->printInfo();

    std::string name = temp->name();

    d_avatars.emplace(name, temp.release());
  }

  //attachments
  dirit = std::filesystem::directory_iterator(inputdir, ec);
  if (ec)
  {
    std::cout << "Error iterating directory `" << inputdir << "' : " << ec.message() << std::endl;
    return;
  }
  for (auto const &att : dirit)
  {
    if (att.path().extension() != ".sbf" || att.path().filename().string().substr(0, STRLEN("Attachment_")) != "Attachment_")
      continue;

    std::filesystem::path attframe = att.path();
    std::filesystem::path attbin = att.path();
    attbin.replace_extension(".bin");

    std::unique_ptr<AttachmentFrame> temp;
    if (!setFrameFromFile(&temp, attframe.string()))
      return;
    temp->setAttachmentData(attbin.string());

    uint64_t rowid = temp->rowId();
    uint64_t attachmentid = temp->attachmentId();
    d_attachments.emplace(std::make_pair(rowid, attachmentid), temp.release());
  }

  //stickers
  dirit = std::filesystem::directory_iterator(inputdir, ec);
  if (ec)
  {
    std::cout << "Error iterating directory `" << inputdir << "' : " << ec.message() << std::endl;
    return;
  }
  for (auto const &sticker : dirit)
  {
    if (sticker.path().extension() != ".sbf" || sticker.path().filename().string().substr(0, STRLEN("Sticker_")) != "Sticker_")
      continue;

    std::filesystem::path stickerframe = sticker.path();
    std::filesystem::path stickerbin = sticker.path();
    stickerbin.replace_extension(".bin");

    std::unique_ptr<StickerFrame> temp;
    if (!setFrameFromFile(&temp, stickerframe.string()))
      return;
    temp->setAttachmentData(stickerbin.string());

    uint64_t rowid = temp->rowId();
    d_stickers.emplace(std::make_pair(rowid, temp.release()));
  }


  d_ok = true;
}
