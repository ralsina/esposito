<section class="hero-section" style="position: relative;">
    <h2 style="color: var(--b16-base0D);">Cheap is <i>good</i></h2>

  <div class="hero-actions">
      <a href="#Getting-Started" class="primary contrast" role="button">Get Started</span>
      <a href="https://github.com/ralsina/esposito" role="button" class="contrast">GitHub</a>
  </div>
</section>

{{% tag section class="features-section" %}}
{{% tag div class="grid" %}}

{{% card %}}
### 💸 [Cheap](#Cheap) 

How about instead of making an expensive computer do more, we make the cheapest computer do *a lot*?
{{% /card %}}

{{% card %}}
### 📦 [Feature-Rich](#Apps) ➚</a>

Apps, settings, fonts, graphics, games, and more!
{{% /card %}}

{{% card %}}
### 🎯 [Simple](#Simple)

You want to create an app? Use C or C++. Get enough API to make it interesting.
{{% /card %}}

{{% card %}}
### 🛠️ [Getting Started](#Getting-Started)

Fully Open Source. Buy the cheapest computer and get started.

{{% /card %}}

{{% /tag %}}
{{% /tag %}}

## Cheap

Because of Moore's law, our "good" computers get faster and faster. This should
also imply that at some point our worst computer is going to be fast. I think
that day has come.

You can buy a "Cheap Yellow Display" for $10. That is a dual core CPU, gigabytes 
of storage, a full color touchscreen and *more.* If we treat it like a real computer,
that is a $10 computer. In our modern world, this is hundreds of times slower than
your phone. But ... it is hundreds of times faster than a Palm Pilot. And on palm
pilot apps started instantly. And the device turned on/off in under a second. Have
you seen how long your *good* pocket computer takes to TURN OFF?

So, let's take something cheap and make it *good*.

# Simple

I wrote ESPOsito to bring that speed, that snap, into the 21st century. It *should* boot 
in under a second and show you the same app you were using before, in the same state.
It should turn off instantly. Hell, you should be able to turn it off by CUTTING POWER.

Apps load into memory then are COMPLETELY replaced when another app starts. No multitasking.
Just do your thing in your app. Try not to crash because it reboots.

You can write a useful app in some hundred lines of C. The API is still in flux, but it
is enough for now.

## Apps

And it works. It really works. While this is still a rather young project we can use it
as an ebook reader. For books of any size. In markdown:

{{% tag div class="grid" %}}
{{% card %}}
{{% figure src="reader.png" caption="Real screenshot" link="https://github.com/ralsina/esposito/tree/main/apps/reader" %}}
{{% /card %}}

{{% card %}}
{{% figure src="reader2.png" caption="In action" link="https://github.com/ralsina/esposito/tree/main/apps/reader" %}}
{{% /card %}}
{{% /tag %}}

It can have as many apps installed as you want and will fit in your SD card. They start in milliseconds, they restore their
state so even without multitasking you can actually switch apps faster than on "good" hardware. There are also graphical
apps like [paint](https://github.com/ralsina/esposito/tree/main/apps/paint), but I can't screenshot them yet.

{{% tag div class="grid" %}}
{{% card %}}
{{% figure src="filemanager.png" caption="File Manager" link="https://github.com/ralsina/esposito/tree/main/apps/filemanager" %}}
{{% /card %}}
{{% card %}}
{{% figure src="snake.png" caption="Snake Game" link="https://github.com/ralsina/esposito/tree/main/apps/snake" %}}
{{% /card %}}
{{% card %}}
{{% figure src="clock.png" caption="Clock, with NTP and weather" link="https://github.com/ralsina/esposito/tree/main/apps/clock" %}}
{{% /card %}}
{{% card %}}
{{% figure src="kilo.png" caption="The Kilo text editor" link="https://github.com/ralsina/esposito/tree/main/apps/kilo" %}}
{{% /card %}}
{{% card %}}
{{% figure src="terminal.png" caption="A serial terminal" link="https://github.com/ralsina/esposito/tree/main/apps/terminado" %}}
{{% /card %}}
{{% card %}}
{{% figure src="lali.png" caption="AI Chat Buddy" link="https://github.com/ralsina/esposito/tree/main/apps/lali" %}}
{{% /card %}}
{{% card %}}
{{% figure src="calc.png" caption="Calculator" link="https://github.com/ralsina/esposito/tree/main/apps/calc" %}}
{{% /card %}}
{{% card %}}
{{% figure src="launcher.png" caption="App Launcher / Switcher" %}}
{{% /card %}}
{{% /tag %}}

Of course, if you want to type a lot the on screen keyboard may not be great. Which is why you can, of course, add a keyboard. I use a [BBQ20 by solder party](https://www.tindie.com/products/arturo182/bb-q20-keyboard-with-trackpad-usbi2cpmod/)

{{% tag div class="grid" %}}
{{% card %}}
{{% figure src="bberry.png" caption="BlackBerry Style" %}}
{{% /card %}}
{{% card %}}
{{% figure src="bberry2.png" caption="Yes, you can rotate the screen" %}}
{{% /card %}}
{{% card %}}
{{% figure src="esposito.jpg" caption="Candybar" %}}
{{% /card %}}
{{% card %}}
{{% figure src="clamshell.png" caption="Clamshell (WIP)" %}}
{{% /card %}}
{{% /tag %}}

It's small, it's technically simple, it defines a very basic API for apps to implement whatever they want.

## Getting Started

1. Get a CYD [somewhere.](https://es.aliexpress.com/w/wholesale-2432s028.html?spm=a2g0o.productlist.search.0)
2. Go to [github](https://github.com/ralsina/esposito)
3. Let's figure it out together
4. Maybe get a 3d printer and make a nicer case?
5. Write some apps!

Step 3 is because ... well, I only used this in *my* hardware. Yours may be a bit different. For example, I
have a "2 USB" CYD, which is not the original. The screen driver is different. But if you are willing to put
some effort, this project is SIMPLE by most microcontroller standards :-)

I want this to work on more devices. I want it for e-ink. I want it for better systems. Systems with a decent
memory size. Systems from the future, when our cheapest computer is even *better* than this one.

But I want it to *feel* like this. Snappy. Simple. Fun.
