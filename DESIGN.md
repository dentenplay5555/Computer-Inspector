# Design direction

Source: the project owner's answers to the design brief (2026-11-26). This file only records those answers.

## Scope

The native server window (`server_gui`): the screen the person running the server sees.

The teacher web dashboard (`dashboard/index.html`) uses the same direction (owner's answer, 2026-11-26).

## Identity

Computer Inspector for Classroom, server side.

## Personality

Friendly classroom tool: warm and approachable for teachers who are not technical. Bigger text, softer tones.

## Theme

Light and dark, with a toggle in the window.

## Palette

Mostly neutral greys with a single teal accent. Status colors (ok / warning / critical / offline) are separate and only mark machine state.

## Typography

Noto Sans Thai (OFL), bundled into the build. Noto Sans supplies the Latin glyphs.

Web dashboard: Noto Sans Thai when installed on the viewing computer; otherwise the system's Thai font, since a classroom may have no internet to download web fonts.

## Language

Thai and English, with a switch in the window.
