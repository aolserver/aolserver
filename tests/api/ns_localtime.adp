<xmp>
format:

    seconds (0-59)
    minutes (0-59)
    hours (0-23)
    dayofmonth (1-31)
    monthofyear (0-11)
    year (year-1900)*
    dayofweek (Sunday=0)
    dayofyear (0-365)
    1 if Daylight Savings Time is in effect
</xmp>

<hr size="1" noshade>

<xmp>
            [ns_localtime] = <%= [ns_localtime] %>

               [ns_gmtime] = <%= [ns_gmtime] %>
        [ns_localtime GMT] = <%= [ns_localtime GMT] %>
        [ns_localtime UTC] = <%= [ns_localtime UTC] %>

    [ns_localtime EST5EDT] = <%= [ns_localtime EST5EDT] %>
    [ns_localtime CST6CDT] = <%= [ns_localtime CST6CDT] %>
    [ns_localtime MST7MDT] = <%= [ns_localtime MST7MDT] %>
    [ns_localtime PST8PDT] = <%= [ns_localtime PST8PDT] %>

 [ns_localtime US/Eastern] = <%= [ns_localtime US/Eastern] %>
 [ns_localtime US/Central] = <%= [ns_localtime US/Central] %>
[ns_localtime US/Mountain] = <%= [ns_localtime US/Mountain] %>
 [ns_localtime US/Pacific] = <%= [ns_localtime US/Pacific] %>

GMT offset based timezones are defined by POSIX as being
east of UTC instead of west, or basically opposite of what
you'd expect.

      [ns_localtime GMT+5] = <%= [ns_localtime GMT+5] %>
      [ns_localtime GMT+4] = <%= [ns_localtime GMT+4] %>
      [ns_localtime GMT+3] = <%= [ns_localtime GMT+3] %>
      [ns_localtime GMT+2] = <%= [ns_localtime GMT+2] %>
      [ns_localtime GMT+1] = <%= [ns_localtime GMT+1] %>

        [ns_localtime GMT] = <%= [ns_localtime GMT] %>

      [ns_localtime GMT-1] = <%= [ns_localtime GMT-1] %>
      [ns_localtime GMT-2] = <%= [ns_localtime GMT-2] %>
      [ns_localtime GMT-3] = <%= [ns_localtime GMT-3] %>
      [ns_localtime GMT-4] = <%= [ns_localtime GMT-4] %>
      [ns_localtime GMT-5] = <%= [ns_localtime GMT-5] %>
</xmp>

