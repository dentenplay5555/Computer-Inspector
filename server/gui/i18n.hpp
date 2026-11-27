#pragma once
// ui strings for the server window, thai and english
enum lang { TH=0, EN=1 };
enum sid {
  S_TITLE, S_SUBTITLE,
  S_LANG_SWITCH, S_DARK, S_LIGHT,
  S_STARTING, S_RUNNING, S_FAILED, S_RETRY,
  S_NETWORK, S_NO_NETWORK, S_AGENTS_ON, S_DASH_OPEN,
  S_AGENT_ADDR, S_AGENT_ADDR_HINT, S_DASH_ADDR, S_DASH_ADDR_HINT,
  S_COPY, S_COPIED, S_OPEN,
  S_COMPUTERS, S_FIND, S_FIND_HINT,
  S_OK, S_WARN, S_CRIT, S_OFFLINE,
  S_COL_PC, S_COL_STATUS, S_COL_CPU, S_COL_RAM, S_COL_DISK, S_COL_DOWN, S_COL_UP, S_COL_IP, S_COL_OS, S_COL_SEEN,
  S_SEC_AGO, S_MIN_AGO, S_HOUR_AGO, S_DAY_AGO, S_DAYS_AGO,
  S_EMPTY, S_EMPTY_HINT, S_NO_MATCH, S_WAIT_START, S_FAILED_TABLE,
  S_LOG, S_CLEAR, S_LOG_EMPTY,
  S_COUNT
};
inline const char* const STR[2][S_COUNT] = {
  { // thai
    "Computer Inspector", "เซิร์ฟเวอร์ห้องเรียน",
    "English", "โหมดมืด", "โหมดสว่าง",
    "กำลังเริ่มเซิร์ฟเวอร์...", "กำลังทำงาน", "เริ่มเซิร์ฟเวอร์ไม่สำเร็จ", "ลองอีกครั้ง",
    "เครือข่าย", "ไม่พบเครือข่าย IPv4 เครื่องอื่นจะเชื่อมต่อไม่ได้", "agent ที่เชื่อมต่อ", "แดชบอร์ดที่เปิด",
    "ลิงก์สำหรับ agent", "ใส่ในคำสั่ง agent บนเครื่องนักเรียน", "แดชบอร์ดสำหรับครู", "เปิดได้จากเครื่องใดก็ได้ในวงแลนเดียวกัน",
    "คัดลอก", "คัดลอกแล้ว", "เปิดในเบราว์เซอร์",
    "เครื่องนักเรียน", "ค้นหาเครื่อง", "ชื่อเครื่องหรือ IP",
    "ปกติ", "เฝ้าระวัง", "วิกฤต", "ออฟไลน์",
    "เครื่อง", "สถานะ", "CPU", "RAM", "ดิสก์", "รับ (KB/s)", "ส่ง (KB/s)", "IP", "ระบบ", "อัปเดตล่าสุด",
    "%d วินาทีก่อน", "%d นาทีก่อน", "%d ชั่วโมง %d นาทีก่อน", "%d วัน %d ชั่วโมงก่อน", "%d วัน %d ชั่วโมงก่อน",
    "ยังไม่มีเครื่องเชื่อมต่อ", "รันคำสั่งนี้บนเครื่องนักเรียนแต่ละเครื่อง:", "ไม่พบเครื่องที่ตรงกับ \"%s\"",
    "รอเซิร์ฟเวอร์เริ่มทำงาน...", "เซิร์ฟเวอร์ยังไม่ทำงาน จึงยังรับข้อมูลจากเครื่องนักเรียนไม่ได้",
    "บันทึกเซิร์ฟเวอร์", "ล้าง", "ยังไม่มีบันทึก",
  },
  { // english
    "Computer Inspector", "Classroom server",
    "ภาษาไทย", "Dark mode", "Light mode",
    "Starting the server...", "Running", "The server could not start", "Try again",
    "Network", "No IPv4 network found. Other PCs cannot reach this server.", "Agents connected", "Dashboards open",
    "Address for agents", "Put this in the agent command on each student PC", "Teacher dashboard", "Opens from any PC on the same LAN",
    "Copy", "Copied", "Open in browser",
    "Student computers", "Find a computer", "Name or IP",
    "Normal", "Watch", "Critical", "Offline",
    "Computer", "Status", "CPU", "RAM", "Disk", "Down (KB/s)", "Up (KB/s)", "IP", "OS", "Last update",
    "%d s ago", "%d min ago", "%d h %d min ago", "%d day %d h ago", "%d days %d h ago",
    "No computers have connected yet", "Run this on each student PC:", "No computer matches \"%s\"",
    "Waiting for the server to start...", "The server is not running, so it cannot receive data from student PCs.",
    "Server log", "Clear", "Nothing logged yet",
  },
};
