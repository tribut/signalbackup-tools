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

#include "sqlitedb.ih"

void SqliteDB::QueryResults::prettyPrint() const
{
  if (rows() == 0 && columns() == 0)
  {
    std::cout << "(no results)" << std::endl;
    return;
  }

  std::setlocale(LC_ALL, "en_US.utf8");
  std::freopen(nullptr, "a", stdout);

  std::vector<std::vector<std::wstring>> contents;

  // add headers
  contents.resize(contents.size() + 1);
  for (unsigned int i = 0; i < d_headers.size(); ++i)
    contents.back().emplace_back(wideString(d_headers[i]));

  // set data
  for (unsigned int i = 0; i < rows(); ++i)
  {
    contents.resize(contents.size() + 1);
    for (uint j = 0; j < columns(); ++j)
    {
      if (valueHasType<std::string>(i, j))
      {
        contents.back().emplace_back(wideString(getValueAs<std::string>(i, j)));
      }
      else if (valueHasType<long long int>(i, j))
      {
        contents.back().emplace_back(bepaald::toWString(getValueAs<long long int>(i, j)));
      }
      else if (valueHasType<double>(i, j))
      {
        contents.back().emplace_back(bepaald::toWString(getValueAs<double>(i, j)));
      }
      else if (valueHasType<std::nullptr_t>(i, j))
      {
        contents.back().emplace_back(L"(NULL)");
      }
      else if (valueHasType<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j))
      {
        contents.back().emplace_back(bepaald::bytesToHexWString(getValueAs<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j).first.get(),
                                                                getValueAs<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j).second));
      }
      else
      {
        contents.back().emplace_back(L"(unhandled type)");
      }
    }
  }

  // calulate widths
  std::vector<uint> widths(contents[0].size(), 0);
  for (uint col = 0; col < contents[0].size(); ++col)
    for (uint row = 0; row < contents.size(); ++row)
      if (widths[col] < contents[row][col].length())
        widths[col] = contents[row][col].length();

  int totalw = std::accumulate(widths.begin(), widths.end(), 0) + 3 * columns() + 1;
  //std::wcout << L" total width: " << totalw << std::endl;
  //std::wcout << L" available width: " << availableWidth() << std::endl;

  if (totalw > availableWidth())
  {
    uint fairwidthpercol = (availableWidth() - 1) / contents[0].size() - 3;
    //std::wcout << L"cols: " << contents[0].size() << L" size per col (adjusted for table space): " << fairwidthpercol << L" LEFT: " << availableWidth() - (contents[0].size() * fairwidthpercol + 3 * contents[0].size() + 1) << std::endl;
    int spaceleftbyshortcols = availableWidth() - (contents[0].size() * fairwidthpercol + 3 * contents[0].size() + 1);
    std::vector<int> oversizedcols;
    uint widestcol = 0;
    uint maxwidth = 0;
    // each column has availableWidth() / nCols (- tableedges) avaliable by fairness.
    // add to this all the space not needed by the columns that are
    // less wide than availableWidth() / nCols anyway.
    for (uint i = 0; i < widths.size(); ++i)
    {
      if (widths[i] > maxwidth)
      {
        maxwidth = widths[i];
        widestcol = i;
      }
      if (widths[i] <= fairwidthpercol)
      {
        spaceleftbyshortcols += fairwidthpercol - widths[i];
        //std::wcout << L"Column " << i << L" has room to spare: " << L" " << widths[i] << L" (" << fairwidthpercol - widths[i] << L")" << std::endl;
      }
      else
      {
        oversizedcols.push_back(i);
        //std::wcout << L"Column " << i << L" is oversized: " << L" " << widths[i] << std::endl;
      }
    }

    uint maxwidthpercol = fairwidthpercol + spaceleftbyshortcols / oversizedcols.size();
    int leftforlongcols = spaceleftbyshortcols % oversizedcols.size();
    //std::wcout << L"Real max width per col: " << maxwidthpercol << L" (left " << leftforlongcols << L")" << std::endl;

    // maybe some oversized column are not oversized anymore...
    bool changed = true;
    while (changed)
    {
      changed = false;
      for (auto it = oversizedcols.begin(); it != oversizedcols.end(); ++it)
      {
        if (widths[*it] < fairwidthpercol + spaceleftbyshortcols / oversizedcols.size())
        {
          //std::wcout << L"Column has room, needs " << widths[*it] << L" available: " << fairwidthpercol + spaceleftbyshortcols / oversizedcols.size() << L" adds " << (fairwidthpercol + spaceleftbyshortcols / oversizedcols.size()) - widths[*it] << L" to pool of leftspace" << std::endl;
          changed = true;
          maxwidthpercol += (fairwidthpercol + spaceleftbyshortcols / oversizedcols.size()) - widths[*it];
          oversizedcols.erase(it);
          break;
        }
        //else
        //{
        //  std::wcout << L"Column needs that extra space" << std::endl;
        //}
      }
      //std::wcout << L"" << std::endl;
    }

    //std::wcout << L"REAL max width per col: " << maxwidthpercol << L" (left " << leftforlongcols << L")" << std::endl;

    // update widths
    widths.clear();
    widths.resize(contents[0].size());
    for (uint col = 0; col < contents[0].size(); ++col)
      for (uint row = 0; row < contents.size(); ++row)
      {
        if (contents[row][col].length() > maxwidthpercol + ((col == widestcol) ? leftforlongcols : 0))
        {
          contents[row][col].resize(maxwidthpercol + ((col == widestcol) ? leftforlongcols : 0) - 5);
          contents[row][col] += L"[...]";
        }
        if (widths[col] < contents[row][col].length())
        {
          widths[col] = contents[row][col].length();
        }
      }
  }

  //std::wcout << std::wstring(availableWidth(), L'*') << std::endl;

  std::wcout << std::wstring(std::accumulate(widths.begin(), widths.end(), 0) + 2 * columns() + columns() + 1, L'-') << std::endl;
  for (uint i = 0; i < contents.size(); ++i)
  {
    std::wcout.setf(std::ios_base::left);
    for (uint j = 0; j < contents[i].size(); ++j)
      std::wcout << L"| " << std::setw(widths[j]) << std::setfill(L' ') << contents[i][j] << std::setw(0) << L" ";
    std::wcout << L"|" << std::endl;

    // another bar under top row
    if (i == 0)
      std::wcout << std::wstring(std::accumulate(widths.begin(), widths.end(), 0) + 2 * columns() + columns() + 1, L'-') << std::endl;
  }
  std::wcout << std::wstring(std::accumulate(widths.begin(), widths.end(), 0) + 2 * columns() + columns() + 1, L'-') << std::endl;

  std::freopen(nullptr, "a", stdout);

  return;
}
