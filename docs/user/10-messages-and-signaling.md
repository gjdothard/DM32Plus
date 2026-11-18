<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Messages, Call Alert and Radio Check

Text messages, call alert and radio check work on DMR channels. The [Radio reference](23-radios.md) says which radios
have them verified. None of them needs the codec. Sending needs a DMR channel you may transmit on
and your DMR ID.

## Receiving a message

When a message arrives, the radio shows a banner with the sender and the first words for 8 s. Press **#** while the
banner is up to open the message. The radio plays a short three-note tone with the banner unless **Settings > Sound >
Message tone** is off. On a radio with a vibration motor it also vibrates twice, unless **Settings > Sound > Vibrate**
is off. A call alert or a private call to you gives one long vibration, an emergency four.

<img src="../ux/screens/240x320i10x5/banner_message.png" width="200" alt="A message banner">

After the banner, the status line shows "M" and the number of unread messages, and the hub shows "1 new" next to
Messages.

## Reading messages

1. Press **OK**, **2** (Messages). Each row names the other radio or talkgroup. The newest conversation is first at the top.
2. Press **OK** to open a conversation. On the dark card layout, bubbles expand to show the full message. Press **OK** to write a reply to the conversation.
3. A live received message opened from the banner or the volatile inbox is marked read and has these actions below the text:

   | Row | Does |
   |---|---|
   | Reply "OK" | Sends "OK" to the message's reply target |
   | Reply "On my way" | Sends "On my way" to the message's reply target |
   | Call | Temporarily selects the message's reply target for voice |
   | Delete | Deletes the message. There is no undo |

The reply target is the sender for a private message and the destination talkgroup for a group message.
**#** opens actions for that target. **PTT** calls that target from the open message.

<img src="../ux/screens/240x320i10x5/messages.png" width="220" alt="Live conversation cards when archive history is unavailable">
<img src="../ux/screens/240x320i10x5/message_view.png" width="200" alt="A message">

The radio keeps the last 4 messages received, in memory. If an open message drops out of the inbox,
the screen reports that the message is gone. The inbox is cleared when the radio turns off.

Builds with a message history reserve prepare it automatically when it is blank. If the reserve already contains
the message archive, it is used as-is. With message history active, **Messages** opens directly to one row per
private contact or talkgroup, including threads you started. The other party or talkgroup names the row.
The dark card layout shows the latest message preview and its time beneath that conversation name.

If needed, **Settings → Advanced → Message history** retries preparing the
radio's reserved storage. Select **Enable** to confirm. The radio reports whether it could
activate the reserve. If it reports that blank storage is required, the existing bytes are
preserved. Once enabled, new received messages and outgoing send results are saved in the
archive. Under **Messages**, choose one of the six most recent private or
talkgroup conversations. In the dark card layout, the conversation opens at the newest bubble at the bottom.
Use Up or Down to move one message at a time, or hold an arrow to keep scrolling. Scrolling stops at either end. The thread fills the viewing area, and a bubble can be partially visible at its edge. **#** jumps to the newest. **OK** opens the text
composer for the thread. Sending a reply returns to that same thread. Delivery or failure appears on the outgoing bubble. New messages for an open conversation
appear at the bottom automatically. A message from a different conversation still shows its banner.
The standard layout opens the newest full message first.
When history is unavailable, Messages groups the four live received messages by the other party or talkgroup. On the dark card layout, unread live messages have a dot
and each live inbox card shows the other party, local receipt time, and a short preview. Inside a conversation, **You** identifies this radio's DMR ID.
The radio takes UTC from a recent onboard GPS fix or a valid retained hardware clock and applies
**Settings → Location → UTC offset** when it displays
the time in 24-hour format. If no time was captured, including for older archive entries,
the time is shown as `--:--`. Incoming messages show the sender first. Archived outgoing messages show the time first,
then the recipient and a **You** label with the send result. The outgoing time is when that result was recorded.
Select a conversation to read the full messages. The dark layout also applies to the canned-message list and the text composer.
Archived text is stored in plaintext.

<img src="../ux/screens/240x320i10x5/message_conversations.png" width="220" alt="Saved conversation directory with previews and local times">
<img src="../ux/screens/240x320i10x5/message_thread.png" width="220" alt="Full incoming and outgoing conversation bubbles">
<img src="../ux/screens/240x320i10x5/message_thread_older.png" width="220" alt="Scrolling into older conversation history">

## Sending a message

1. Open the actions for the person or talkgroup. Press **#** on Home for the last caller, or **#** on a contact.
2. Press **3** (Message).
3. Pick a text ("OK", "On my way", "Call me" or "Can't talk now"). Press **OK** to send. The texts are sent in the
   radio's language (Settings > Display > Language). To write your own message, select **Write...** instead.
4. In **Write...**, press a digit repeatedly to cycle through its letters. Press **#** to change between uppercase,
   digits, and lowercase. **Back** deletes a character and leaves when the draft is empty. Press **OK** to send
   up to 140 characters. On radios without a numeric keypad, use Up and Down to change the current character,
   **OK** to add another character, and hold **OK** to send.

<img src="../ux/screens/240x320i10x5/compose.png" width="200" alt="The message composer">

The radio shows "Sending to ..." and returns to the open conversation when replying from a thread, otherwise Home.
A message to a person asks for a confirmation. When none comes, the radio tries again, in every format. The result
comes as a banner:

| Banner | Meaning |
|---|---|
| "Delivered to ..." | The person's radio confirmed the message |
| "No answer from ...", with "Not delivered: send it again" | No confirmation came. The message isn't kept for a retry. Send it again from the actions |
| "Sent to ..." | A message to a talkgroup has gone out. Talkgroup messages have no confirmation |

With confirmed IP data turned off (`tools/rfoundry_debug.py data ip-settings auto off`), ETSI messages to a person also go
once, with "Sent to".

With message history active, outgoing messages and their send results are stored in the archive.
Without history, outgoing messages are not retained after sending.

## Message formats

**Settings > Connections > Message format** picks the format of the messages you send. The radio reads all three
formats, whatever the setting. A reply goes in the format of the message it answers.

| Format | Use it for |
|---|---|
| ETSI (default) | The standard DMR text message (ETSI IP text). Radios and dispatch software set to "DMR standard" text read it. Delivery to a person is confirmed |
| ETSI short | Radios that only show ETSI short data messages. Delivery is confirmed |
| Motorola | MOTOTRBO radios and dispatch software set to Motorola text messages (TMS). Delivery is confirmed. Not in builds made without Motorola TMS support |

ETSI messages follow the IP header-compression setting: Auto uses compressed headers; Off uses full IP headers.
The recipient may accept one encoding but not the other. Header compression and delivery confirmation are separate:
a text can arrive even when the sender reports no answer. A confirmation means the receiving radio acknowledged it,
not that a person read it.

If your radio was set up before ETSI became the default and its data settings were saved, it keeps sending Motorola
messages until you change the setting.

## Call alert

A call alert pages another radio. It rings there, even when that radio's user isn't listening to your talkgroup.

1. Open the actions for the person (**#** on Home, or **#** on a contact).
2. Press **5** (Call alert). The radio shows "Call alert to ..." and returns to Home.
3. The radio tries up to 3 times. The result is a banner: "... answered the alert" or "No answer from ...".

When someone sends you a call alert, the radio shows "Call alert from ..." with "PTT replies". Press **#** while the
banner is up for the caller's actions. Your radio acknowledges the alert unless **Settings > Connections > Answer call
alerts** is off.

<img src="../ux/screens/240x320i10x5/banner_call_alert.png" width="200" alt="A call alert banner">

## Radio check

A radio check asks another radio to answer, without its user doing anything. It shows whether that radio is on and in
range. The default request uses Motorola signaling. A matching reply confirms reachability; a failure can also mean
that the other radio does not support or allow this signaling format.

1. Open the actions for the person.
2. Press **4** (Radio check). The radio shows "Checking ...".
3. The radio tries up to 3 times. The result is a banner. "... answered (0.8 s)" gives the round-trip time. "...: no
   answer" comes with "Off, out of range or elsewhere".

<img src="../ux/screens/240x320i10x5/banner_radio_check.png" width="200" alt="A radio check result">

Your radio answers radio checks from others while **Settings > Connections > Answer radio checks** is on, the default.
An answer is a short transmission the radio makes without you pressing PTT; turn the setting off if you don't want that.

## Registration (ARS) and location (LRRP)

ARS registers the radio with a presence server at power-up and after a channel change. It is off until a server ID is
set. On the card layout, open **GPS, APRS, and Data > ARS & LRRP** to set **Server ID**, inspect status,
or register manually. A server ID of 0 disables registration. **Answer GPS requests** is available on the
same page. The debug link also exposes ARS and LRRP settings.

<img src="../ux/screens/240x320i10x5/ars.png" width="220" alt="ARS and LRRP status and registration controls">

Choose **Position** in the action menu for a private contact to request its current GPS coordinates over DMR.
The target radio needs to support LRRP and have **Settings > Location > Answer GPS requests** enabled if it
runs RadioFoundry. A radio without a GPS fix reports **No GPS fix**. The requester displays **No answer**
if it receives no matching reply. **Position in DMR calls** can stay off.

Open **Last position** in a private contact's actions to see the last position received from that DMR ID.
Successful position replies and GPS reports embedded in received DMR voice calls update the same entry.
The page shows the distance, north-referenced bearing, and age of the report. If your radio has no GPS fix,
it shows the contact's coordinates and age. Select **Request position** on the page to ask for a fresh fix.
An unanswered request or a reply without a fix leaves the previous position available. The radio remembers
the eight most recently updated positions until power-off.
