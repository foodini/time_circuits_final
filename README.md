# Arduino Time Circuits

![Time Circuits Image](https://raw.githubusercontent.com/foodini/time_circuits_final/master/time_circuits_image.jpg)

This is a project I've been working on for.... far too long. There've been [previous iterations]
(https://www.youtube.com/watch?v=cxGm47E_y9M) of this doodad, but [there were issues].
(https://www.youtube.com/watch?v=KKTgsbm41c8)

There was certainly an inspiration from the Back to Future series, but the idea of building a
replica of the device in that series bored me. I like clocks, but I like FUNCTIONAL devices, and
"Where you are, where you're going, and where you've been" isn't very functional to anyone who
hasn't built themselves a real time machine.

What IS useful is a real clock. I frequently work with servers that run on Universal Coordinated
Time (UTC), so having a clock that displays UTC and local time is useful to me. As long as I was
building a two-line clock.... why not three? ("...do it with a little style!") The question of
what to put on the third line languished for a while until it dawned on me what you'd see in a 
clock showing SOLAR time while DRIVING.

Think of it this way: If it takes you ten hours to drive East to the next time zone, it'll be
ELEVEN hours later when you get there. Normally, you just reset your watch when you get there,
but what if we instead had a clock that tracked "Wild West" time. Before time standards, everyone
set their clocks to local noon - they'd use the moment that the sun was highest in the sky as
their standard of time. Why bother? Well, if I'm driving east at about 100kph (at my latitude),
solar time advances by 1.1 times faster than real-time. You can see this difference if you watch
the clock while moving. (Please don't do this if you're the one driving. Duh.)

This clock computes your local solar time every 100ms, taking into account your longitudinal
position, as well as the Earth's position in its orbit. In writing the firmware, I was shocked
to learn that the effect of the orbit on local solar noon varies by something like +- 15 minutes
through the year!
There are still some debug modes languishing in the firmware that should be polished a bit. I'm
sure people will want more features, and I'll do my best to oblige. What I will absolutely NOT
do is make the adjustment for time zones automatic. I did the legwork to make this happen and
came to the conclusion that it simply isn't realistic without giving the clock an SD card and a
way to constantly update its time zone database. To be honest, the largest reason to avoid
this step was because I didn't want to maintain that database. (Time zone definitions change all
the time.) The actual arduino-side processing would have been easy.

## Resources

The project consists of an Arduino .ino file, two Eagle CAD circuit board designs (one for the
displays and one for the Arduino/GPS controller). I'll see if I can work out a way to manage the
Fusion 360 model of the box in GitHub, but in the meantime it's just shared [on Autodesk's
servers].(https://a360.co/2rUt2ZP)

## TODO
- Since I've gotten the thing boxed up, I've occasionally bumped the settings buttons. This
  causes local time to change by 15 minutes. I think we need to require that you hit the
  mode button to get to the time-change state, where you can alter local time. After n
  seconds, it should return to the main display.
- Make the LED test the last of the display modes.
- Add a histogram that tells you the distribution of inter-sentence reports from the GPS.
