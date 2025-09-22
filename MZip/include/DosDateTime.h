#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iomanip>
#include <sstream>

/// DOS date/time format (32-bit)
/// Date: bits 16-31 (day: 1-31, month: 1-12, year: 0-119 [1980-2099])
/// Time: bits 0-15  (seconds/2: 0-29, minutes: 0-59, hours: 0-23)
struct DOSDateTime
{
  union
  {
    struct
    {
      struct
      {
        std::uint16_t Seconds : 5;
        std::uint16_t Minutes : 6;
        std::uint16_t Hours : 5;
      } Time;
      struct
      {
        std::uint16_t Day : 5;
        std::uint16_t Month : 4;
        std::uint16_t Year : 7;
      } Date;
    } Fields;
    std::uint32_t RawValue;
  } Data{};

  DOSDateTime() : DOSDateTime(std::filesystem::file_time_type::clock::now()) {}
  explicit DOSDateTime(const std::filesystem::file_time_type &FileTime) { fromFileTime(FileTime); }
  explicit DOSDateTime(const std::filesystem::path &Path) : DOSDateTime(std::filesystem::last_write_time(Path)) {}

  void fromFileTime(const std::filesystem::file_time_type &FileTime)
  {
    const auto SystemTime = std::chrono::clock_cast<std::chrono::system_clock>(FileTime);
    const auto Days = std::chrono::floor<std::chrono::days>(SystemTime);
    const auto YearMonthDay = std::chrono::year_month_day(Days);
    const auto TimeOfDay = std::chrono::hh_mm_ss(SystemTime - Days);

    Data.Fields.Date = {.Day = static_cast<uint16_t>(static_cast<unsigned>(YearMonthDay.day())),
                        .Month = static_cast<uint16_t>(static_cast<unsigned>(YearMonthDay.month())),
                        .Year = static_cast<uint16_t>(static_cast<int>(YearMonthDay.year()) - 1980)};

    Data.Fields.Time = {.Seconds = static_cast<uint16_t>(TimeOfDay.seconds().count() / 2),
                        .Minutes = static_cast<uint16_t>(TimeOfDay.minutes().count()),
                        .Hours = static_cast<uint16_t>(TimeOfDay.hours().count())};
  }

  [[nodiscard]] std::filesystem::file_time_type toFileTime() const
  {
    const auto TimePoint =
        std::chrono::sys_days(std::chrono::year(1980 + Data.Fields.Date.Year) / std::chrono::month(Data.Fields.Date.Month) /
                              std::chrono::day(Data.Fields.Date.Day)) +
        std::chrono::hours(Data.Fields.Time.Hours) + std::chrono::minutes(Data.Fields.Time.Minutes) +
        std::chrono::seconds(Data.Fields.Time.Seconds * 2);

    return std::chrono::clock_cast<std::filesystem::file_time_type::clock>(TimePoint);
  }

  [[nodiscard]] std::string toString(bool UseLocale = true) const
  {
    if (!UseLocale)
    {
      return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", 1980 + Data.Fields.Date.Year, Data.Fields.Date.Month,
                         Data.Fields.Date.Day, Data.Fields.Time.Hours, Data.Fields.Time.Minutes,
                         Data.Fields.Time.Seconds * 2);
    }

    std::tm Tm{};
    Tm.tm_year = Data.Fields.Date.Year + 80;
    Tm.tm_mon = Data.Fields.Date.Month - 1;
    Tm.tm_mday = Data.Fields.Date.Day;
    Tm.tm_hour = Data.Fields.Time.Hours;
    Tm.tm_min = Data.Fields.Time.Minutes;
    Tm.tm_sec = Data.Fields.Time.Seconds * 2;

    std::ostringstream Ss;
    Ss.imbue(std::locale(""));
    auto Time = std::mktime(&Tm);
    Ss << std::put_time(std::localtime(&Time), "%x %X");
    return Ss.str();
  }

  [[nodiscard]] constexpr auto operator<=>(const DOSDateTime &Other) const noexcept
  {
    return Data.RawValue <=> Other.Data.RawValue;
  }
  constexpr bool operator==(const DOSDateTime &Other) const noexcept { return Data.RawValue == Other.Data.RawValue; }
};