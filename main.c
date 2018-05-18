#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define N 32
#define QUEUE_SIZE 256

typedef unsigned char uchar;

enum {
	THEME_WIN9X,
	THEME_WIN2K,
};

const TCHAR appname[] = TEXT("Minesweeper");
const struct {
	int height;
	int width;
	int num_mines;
} board_config[3] = {
	{ 9, 9,10},
	{16,16,40},
	{16,30,99},
};

HINSTANCE the_instance;
HWND main_window;
HMENU main_menu;

int cycaption1;
int cymenu1;
int cyborder1;
int cxborder1;
int view_width, view_height;

HGDIOBJ shade_pen;
HDC tile_dc[16];
uchar *tile_bmpdata;
uchar *digit_bmpdata;
int digit_bmpdata_offset[12];
uchar *face_bmpdata;
int face_bmpdata_offset[5];

int board_type;
int boardh, boardw;
int num_mines;
int lastx;
int lasty;
int num_safe_tiles;
int num_opened_tiles;
int minectr;
int time_elapsed;
int face_state;
int qend;
int theme;

uchar game_state;
uchar ticking;
uchar sound;
uchar solver_enabled;

uchar mouse_captured;
uchar midbutton_down;
uchar just_activated;
uchar in_menu_loop;
uchar window_hidden;

uchar board[N][N];

typedef struct {
	uchar x, y;
} Point;

Point floodfillqueue[QUEUE_SIZE];

void init_graphics(void);
void load_graphics(void);
void reveal_board(uchar);
void open_safe_tile(int x, int y);
void open_safe_tile_ui(int x, int y);
int count_flags_around(int x, int y);
void end_game(uchar won);

void
select_pen(HDC dc, uchar style)
{
	if (style&1) {
		SetROP2(dc, R2_WHITE);
	} else {
		SetROP2(dc, R2_COPYPEN);
		SelectObject(dc, shade_pen);
	}
}

void
paint_frame(HDC dc, int x1, int y1, int x2, int y2, int width, uchar style)
{
	select_pen(dc, style);
	if (width>0) {
		int n1 = width;
		do {
			MoveToEx(dc, x1, --y2, 0);
			LineTo(dc, x1++, y1);
			LineTo(dc, x2--, y1++);
			n1--;
		} while (n1);
	}
	if (style<2) select_pen(dc, style^1);
	if (width>0) {
		int n1 = width;
		do {
			MoveToEx(dc, x1--, ++y2, 0);
			LineTo(dc, ++x2, y2);
			LineTo(dc, x2, --y1);
			n1--;
		} while (n1);
	}
}

void
paint_frames(HDC dc)
{
	int x = view_width-1;
	int y = view_height-1;
	paint_frame(dc, 0, 0, x, y, 3, 1);
	x -= 9;
	y -= 9;
	paint_frame(dc, 9, 52, x, y, 3, 0);
	paint_frame(dc, 9, 9, x, 45, 2, 0);
	paint_frame(dc, 16, 15, 56, 39, 1, 0);
	x = view_width - cxborder1 - 57;
	paint_frame(dc, x, 15, x+40, 39, 1, 0);
	x = ((view_width-24)>>1)-1;
	paint_frame(dc, x, 15, x+25, 40, 1, 2);
}

void
paint_digit(HDC dc, int x, int digit)
{
	SetDIBitsToDevice(dc,
			  x, 16, 13, 23,
			  0, 0, 0, 23,
			  digit_bmpdata + digit_bmpdata_offset[digit],
			  (BITMAPINFO *) digit_bmpdata, 0);
}

void
paint_face(HDC dc, int id)
{
	SetDIBitsToDevice(dc,
			  (view_width-24)>>1, 16, 24, 24,
			  0, 0, 0, 24,
			  face_bmpdata + face_bmpdata_offset[id],
			  (BITMAPINFO *) face_bmpdata, 0);
}

void
paint_timer(HDC dc)
{
	DWORD layout = GetLayout(dc);
	int q, r;
	int x;
	if (layout&1) SetLayout(dc, 0);
	q = time_elapsed/100;
	r = time_elapsed%100;
	x = view_width - cxborder1 - 56;
	paint_digit(dc, x   , q);
	paint_digit(dc, x+13, r/10);
	paint_digit(dc, x+26, r%10);
	if (layout&1) SetLayout(dc, layout);
}

void
paint_mine_counter(HDC dc)
{
	DWORD layout = GetLayout(dc);
	int q, r;
	if (layout&1) SetLayout(dc, 0);
	if (minectr >= 0) {
		q = minectr / 100;
		r = minectr % 100;
	} else {
		q = 11;
		r = - minectr % 100;
	}
	paint_digit(dc, 17, q);
	paint_digit(dc, 30, r/10);
	paint_digit(dc, 43, r%10);
	if (layout&1) SetLayout(dc, layout);
}

void
paint_board(HDC dc)
{
	int y = 55;
	int x;
	int i, j;
	if (boardh >= 1) {
		for (i=1; i <= boardh; i++) {
			x = 12;
			for (j=1; j <= boardw; j++) {
				uchar t = board[i][j] & 15;
				BitBlt(dc,
				       x, y, 16, 16,
				       tile_dc[t], 0, 0, SRCCOPY);
				x += 16;
			}
			y += 16;
		}
	}
}

void
paint_window(HDC dc)
{
	paint_frames(dc);
	paint_timer(dc);
	paint_face(dc, face_state);
	paint_mine_counter(dc);
	paint_board(dc);
}

void
paint_face_getdc(int faceid)
{
	HDC dc = GetDC(main_window);
	paint_face(dc, faceid);
	ReleaseDC(main_window, dc);
}

void
init_board(void)
{
	int i;
	memset(&board[0][0], 15, sizeof board);
	memset(&board[0][0], 16, boardw+2);
	for (i=1; i<=boardh; i++) {
		board[i][0] = 16;
		board[i][boardw+1] = 16;
	}
	memset(&board[boardh+1][0], 16, boardw+2);
}

void
paint_mine_counter_getdc(void)
{
	HDC dc = GetDC(main_window);
	paint_mine_counter(dc);
	ReleaseDC(main_window, dc);
}

#if 0
int
sub_1001915(int sm)
{
	int ret;
	if (sm) {
		if (sm == 1) {
			ret = GetSystemMetrics(SM_CYVIRTUALSCREEN);
			if (!ret) ret = GetSystemMetrics(SM_CYSCREEN);
		} else {
			ret = GetSystemMetrics(sm);
		}
	} else {
		ret = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		if (!ret) ret = GetSystemMetrics(SM_CXSCREEN);
	}
	return ret;
}
#endif

void
update_window(uchar flags)
{
	int v6 = 0;
	RECT r;
	int h;

	h = cycaption1 + cymenu1;
	view_height = boardh*16 + 67;
	view_width = boardw*16 + 24;
	if (!window_hidden) {
		if (flags & 2) {
			SetWindowPos(main_window, 0, 0, 0,
				     view_width + cxborder1, h + view_height,
				     SWP_NOZORDER | SWP_NOMOVE);
		}
		if (flags & 4) {
			SetRect(&r, 0, 0, view_width, view_height);
			InvalidateRect(main_window, &r, 1);
		}
	}
}

int
randint(int n)
{
	return rand()%n;
}

void
place_mines(void)
{
	int n = num_mines;
	while (n) {
		int y, x;
		do {
			y = randint(boardh)+1;
			x = randint(boardw)+1;
		} while (board[y][x]&0x80);
		board[y][x] |= 0x80;
		n--;
	}
}

void
new_game(void)
{
	ticking = 0;
	init_board();
	face_state = 0;
	time_elapsed = 0;
	minectr = num_mines;
	place_mines();
	num_opened_tiles = 0;
	num_safe_tiles = boardw * boardh - num_mines;
	game_state = 1;
	paint_mine_counter_getdc();
	update_window(6);
}

void
check_menu_item(int id, int checked)
{
	CheckMenuItem(main_menu, id, checked?8:0);
}

void
check_menu_items(void)
{
	/* put check marks in front of menu items */
	check_menu_item(521, board_type == 0);
	check_menu_item(522, board_type == 1);
	check_menu_item(523, board_type == 2);
	check_menu_item(524, board_type == 3);
	check_menu_item(526, sound);
	check_menu_item(527, theme == THEME_WIN9X);
	check_menu_item(528, theme == THEME_WIN2K);
	check_menu_item(529, solver_enabled);
}

void
update_menu(void)
{
	check_menu_items();
	SetMenu(main_window, main_menu);
	update_window(2);
}

int
face_hittest(LPARAM lparam)
{
	int y, x;
	MSG msg;
	POINT pt;
	RECT r;
	uchar v3;

	y = HIWORD(lparam);
	x = LOWORD(lparam);
	pt.y = y;
	pt.x = x;
	r.left = (view_width-24)>>1;
	r.right = r.left + 24;
	r.top = 16;
	r.bottom = 40;
	v3 = 1;
	if (!PtInRect(&r, pt)) return 0;
	SetCapture(main_window);
	paint_face_getdc(4);
	MapWindowPoints(main_window, 0, (POINT *) &r, 2);
	do {
		for (;;) {
			while (!PeekMessage(&msg, main_window, WM_MOUSEMOVE,
					    0x20d, 1));
			if (msg.message != WM_MOUSEMOVE) break;
			if (PtInRect(&r, msg.pt)) {
				if (!v3) {
					v3 = 1;
					paint_face_getdc(4);
				}
			} else if (v3) {
				v3 = 0;
				paint_face_getdc(face_state);
			}
		}
	} while (msg.message != WM_LBUTTONUP);
	if (v3) {
		if (PtInRect(&r, msg.pt)) {
			face_state = 0;
			paint_face_getdc(0);
			new_game();
		}
	}
	ReleaseCapture();
	return 1;
}

void
release_tile(int x, int y)
{
	uchar t = board[y][x];
	uchar v3 = t&0x1f;
	if (v3 == 9) {
		v3 = 13;
	} else if (v3 == 0) {
		v3 = 15;
	}
	board[y][x] = (t&0xe0)|v3;
}

void
depress_tile(int x, int y)
{
	uchar t = board[y][x];
	uchar v3 = t&0x1f;
	if (v3 == 13) {
		v3 = 9;
	} else if (v3 == 15) {
		v3 = 0;
	}
	board[y][x] = (t&0xe0)|v3;
}

void
paint_tile(int x, int y)
{
	HDC dc = GetDC(main_window);
	uchar t = board[y][x]&15;
	BitBlt(dc, x*16-4, y*16+39, 16, 16, tile_dc[t], 0, 0, SRCCOPY);
	ReleaseDC(main_window, dc);
}

void
slide_on_tile(int x, int y)
{
	int oldx, oldy;
	if (x == lastx && y == lasty) return;
	oldx = lastx;
	oldy = lasty;
	lastx = x;
	lasty = y;
	if (midbutton_down) {
		int oldy0, oldy1, y0, y1, oldx0, oldx1, x0, x1;
		int i, j;
		uchar v23, v24;
		oldy0 = max(oldy-1, 1);
		oldy1 = min(oldy+1, boardh);
		y0 = max(y-1, 1);
		y1 = min(y+1, boardh);
		oldx0 = max(oldx-1, 1);
		oldx1 = min(oldx+1, boardw);
		x0 = max(x-1, 1);
		x1 = min(x+1, boardw);
		v24 = oldx > 0 && oldy > 0 && oldx <= boardw && oldy <= boardh;
		v23 = x > 0 && y > 0 && x <= boardw && y <= boardh;
		if (v24) {
			for (i=oldy0; i<=oldy1; i++) {
				for (j=oldx0; j<=oldx1; j++) {
					if (!(board[i][j]&0x40)) {
						release_tile(j, i);
					}
				}
			}
		}
		if (v23) {
			for (i=y0; i<=y1; i++) {
				for (j=x0; j<=x1; j++) {
					if (!(board[i][j]&0x40)) {
						depress_tile(j, i);
					}
				}
			}
		}
		if (v24) {
			for (i=oldy0; i<=oldy1; i++) {
				for (j=oldx0; j<=oldx1; j++) {
					paint_tile(j, i);
				}
			}
		}
		if (v23) {
			for (i=y0; i<=y1; i++) {
				for (j=x0; j<=x1; j++) {
					paint_tile(j, i);
				}
			}
		}
	} else {
		if (oldx > 0 && oldy > 0 && oldx <= boardw && oldy <= boardh) {
			if (!(board[oldy][oldx]&0x40)) {
				release_tile(oldx, oldy);
				paint_tile(oldx, oldy);
			}
		}
		if (x > 0 && y > 0 && x <= boardw && y <= boardh) {
			uchar t = board[y][x];
			if (!(t&0x40)) {
				if ((t&0x1f) != 14) {
					depress_tile(x, y);
					paint_tile(x, y);
				}
			}
		}
	}
}

void
toggle_flag(int x, int y)
{
	if (x > 0 && y > 0 && x <= boardw && y <= boardh) {
		uchar t = board[y][x];
		if (!(t&0x40)) {
			uchar v1 = t&0x1f;
			uchar newt;
			if (v1 == 14) {
				newt = 15;
				minectr++;
			} else if (v1 == 15) {
				newt = 14;
				minectr--;
			}
			paint_mine_counter_getdc();
			board[y][x] = (t&0xe0)|newt;
			if (v1 == 14 && num_opened_tiles == num_safe_tiles) {
				end_game(1);
			} else {
				paint_tile(x, y);
			}
		}
	}
}

void
play_sound(int a1)
{
	if (sound != 3 || a1 < 1 || a1 > 3) return;
	PlaySound((LPCTSTR)(431+a1), the_instance, 0x40005);
}

void
paint_timer_getdc(void)
{
	HDC dc = GetDC(main_window);
	paint_timer(dc);
	ReleaseDC(main_window, dc);
}

void
end_game(uchar won)
{
	ticking = 0;
	face_state = won?3:2;
	paint_face_getdc(face_state);
	reveal_board(won?14:10);
	if (won && minectr) {
		minectr = 0;
		paint_mine_counter_getdc();
	}
	play_sound(won?2:3);
	game_state = 0;
}

void
middle_click(int x, int y)
{
	uchar t = board[y][x];
	int i, j;
	if ((t&0x40) && (t&0x1f) == count_flags_around(x, y)) {
		for (i=y-1; i<=y+1; i++) for (j=x-1; j<=x+1; j++) {
			t = board[i][j];
			if ((t&0x80) && (t&0x1f) != 14) {
				board[i][j] = (t&0xe0)|0x4c;
				end_game(0);
			} else {
				open_safe_tile_ui(j, i);
				if (!game_state) return;
			}
		}
	} else {
		slide_on_tile(-2, -2);
	}
}

void
paint_board_getdc(void)
{
	HDC dc = GetDC(main_window);
	paint_board(dc);
	ReleaseDC(main_window, dc);
}

void
reveal_board(uchar a1)
{
	int i, j;
	for (i=1; i<=boardh; i++) for (j=1; j<=boardw; j++) {
		uchar t = board[i][j];
		if (!(t&0x40)) {
			uchar v6 = t&0x1f;
			if (t&0x80) {
				if (v6 != 14) {
					board[i][j] = (t&0xe0)|a1;
				}
			} else {
				if (v6 == 14) {
					/* flagged non-mine */
					board[i][j] = (t&0xe0)|11;
				}
			}
		}
	}
	paint_board_getdc();
}

int
count_mines_around(int x, int y)
{
	int ret = 0;
	int i, j;
	for (i=y-1; i<=y+1; i++) for (j=x-1; j<=x+1; j++) {
		if (board[i][j]&0x80) ret++;
	}
	return ret;
}

int
count_flags_around(int x, int y)
{
	int ret = 0;
	int i, j;
	for (i=y-1; i<=y+1; i++) for (j=x-1; j<=x+1; j++) {
		if ((board[i][j]&0x1f) == 14) ret++;
	}
	return ret;
}

void
floodfill(int x, int y)
{
	uchar t = board[y][x];
	if (!(t&0x40)) {
		uchar v5 = t&0x1f;
		if (v5 != 16 && v5 != 14) {
			uchar nmine;
			num_opened_tiles++;
			nmine = count_mines_around(x, y);
			board[y][x] = nmine | 0x40;
			if (nmine == 0) {
				floodfillqueue[qend].x = x;
				floodfillqueue[qend].y = y;
				if (++qend == QUEUE_SIZE)
					qend = 0;
			}
		}
	}
}

void
open_safe_tile(int x, int y)
{
	int k = 1;
	qend = 1;
	floodfill(x, y);
	if (qend != 1) {
		do {
			int qx = floodfillqueue[k].x;
			int qy = floodfillqueue[k].y;
			floodfill(qx-1, qy-1);
			floodfill(qx  , qy-1);
			floodfill(qx+1, qy-1);
			floodfill(qx-1, qy  );
			floodfill(qx+1, qy  );
			floodfill(qx-1, qy+1);
			floodfill(qx  , qy+1);
			floodfill(qx+1, qy+1);
			if (++k == QUEUE_SIZE) k = 0;
		} while (k != qend);
	}
}

uchar
is_unopened(uchar t)
{
	return !(t&0x40) && (t&0x1f) != 16;
}

void
auto_solve(void)
{
	int i, j;
	uchar changed;
	for (;;) {
		changed = 0;
		/* place flags */
		for (i=1; i<=boardh; i++) for (j=1; j<=boardw; j++) {
			uchar t = board[i][j];
			int nmine;
			int unopened;
			int ii, jj;
			if (!(t&0x40)) continue;
			nmine = t&15;
			unopened = 0;
			for (ii=i-1; ii<=i+1; ii++) for (jj=j-1; jj<=j+1; jj++) {
				if (is_unopened(board[ii][jj])) unopened++;
			}
			if (nmine == unopened) {
				for (ii=i-1; ii<=i+1; ii++) for (jj=j-1; jj<=j+1; jj++) {
					uchar tt = board[ii][jj];
					if (is_unopened(tt) && (tt&0x1f) != 14) {
						board[ii][jj] = (tt&0xe0)|14;
						minectr--;
						changed = 1;
					}
				}
			}
		}
		/* open tiles */
		for (i=1; i<=boardh; i++) for (j=1; j<=boardw; j++) {
			uchar t = board[i][j];
			int ii, jj;
			if (!(t&0x40) || (t&0x1f) != count_flags_around(j, i)) continue;
			for (ii=i-1; ii<=i+1; ii++) for (jj=j-1; jj<=j+1; jj++) {
				uchar tt = board[ii][jj];
				if (is_unopened(tt) && (tt&0x1f) != 14) {
					if (tt&0x80) {
						board[ii][jj] = 0xcc;
						end_game(0);
						return;
					}
					open_safe_tile(jj, ii);
					changed = 1;
				}
			}
		}
		if (!changed) break;
	}
}

void
open_safe_tile_ui(int x, int y)
{
	if (!(board[y][x]&0x40)) {
		HDC dc = GetDC(main_window);
		open_safe_tile(x, y);
		if (solver_enabled) {
			auto_solve();
			paint_mine_counter(dc);
			if (!game_state) return; /* lost */
		}
		if (num_opened_tiles == num_safe_tiles) {
			end_game(1);
		} else {
			paint_board(dc);
		}
		ReleaseDC(main_window, dc);
	}
}

void
open_tile(int x, int y)
{
	uchar t = board[y][x];
	if (t&0x80) {
		if (num_opened_tiles == 0) {
			/* hit a mine on first try... */
			int i, j;
			for (i=1; i<=boardh; i++) for (j=1; j<=boardw; j++) {
				if (!(board[i][j]&0x80)) {
					board[y][x] = 15;
					board[i][j] |= 0x80;
					goto open;
				}
			}
		}
		/* BOOM! */
		board[y][x] = 0x4c;
		end_game(0);
		return;
	}
open:
	open_safe_tile_ui(x, y);
}

void
sub_10037e1(void)
{
	if (lastx > 0) {
		if (lasty > 0 && lastx <= boardw && lasty <= boardh) {
			if (num_opened_tiles == 0 && time_elapsed == 0) {
				play_sound(1);
				time_elapsed++;
				paint_timer_getdc();
				ticking = 1;
				SetTimer(main_window, 1, 1000, 0);
			}
			if (!game_state) {
				lasty = -2;
				lastx = -2;
			}
			if (midbutton_down) {
				middle_click(lastx, lasty);
			} else {
				uchar t = board[lasty][lastx];
				if (!(t&0x40) && (t&0x1f) != 14)
					open_tile(lastx, lasty);
			}
		}
	}
	paint_face_getdc(face_state);
}

void
tick(void)
{
	if (ticking && time_elapsed < 999) {
		time_elapsed++;
		paint_timer_getdc();
		play_sound(1);
	}
}

void
set_board_type(int type)
{
	board_type = type;
	boardh = board_config[type].height;
	boardw = board_config[type].width;
	num_mines = board_config[type].num_mines;
}

int
sub_1003df6(HWND dlg, int itemid, int lo, int hi)
{
	int ret = GetDlgItemInt(dlg, itemid, &itemid, 0);
	if (ret < lo) ret = lo;
	else if (ret > hi) ret = hi;
	return ret;
}

BOOL CALLBACK
custom_field_dlgproc(HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case WM_INITDIALOG:
		SetDlgItemInt(dlg, 141, boardh, 0);
		SetDlgItemInt(dlg, 142, boardw, 0);
		SetDlgItemInt(dlg, 143, num_mines, 0);
		return 1;
	case WM_COMMAND:
		switch (wparam) {
		case 1:
			boardh = sub_1003df6(dlg, 141, 8, N-2);
			boardw = sub_1003df6(dlg, 142, 8, N-2);
			num_mines = sub_1003df6(dlg, 143, 0, min(boardh*boardw, 999));
			/* fallthrough */
		case 2:
			EndDialog(dlg, 1);
			return 1;
		}
		break;
	}
	return 0;
}

LRESULT CALLBACK
wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
		PAINTSTRUCT ps;
		int cmd;
	case WM_DESTROY:
		KillTimer(hwnd, 1);
		PostQuitMessage(0);
		return 0;
	case WM_PAINT:
		paint_window(BeginPaint(hwnd, &ps));
		EndPaint(hwnd, &ps);
		return 0;
	case WM_MBUTTONDOWN:
		if (just_activated) {
			just_activated = 0;
			return 0;
		}
		if (!game_state) break;
		midbutton_down = 1;
		goto capture_mouse;
	case WM_LBUTTONDOWN:
		if (just_activated) {
			just_activated = 0;
			return 0;
		}
		if (face_hittest(lparam))
			return 0;
		if (!game_state) break;
		midbutton_down = (wparam&6) != 0;
		goto capture_mouse;
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
		if (mouse_captured)
			goto release_mouse;
		break;
	case WM_RBUTTONDOWN:
		if (just_activated) {
			just_activated = 0;
			return 0;
		}
		if (!game_state) break;
		if (mouse_captured) {
			slide_on_tile(-3, -3);
			midbutton_down = 1;
			PostMessage(hwnd, WM_MOUSEMOVE, wparam, lparam);
			return 0;
		}
		if (wparam & 1) {
capture_mouse:
			SetCapture(hwnd);
			lastx = -1;
			lasty = -1;
			mouse_captured = 1;
			paint_face_getdc(1);
			goto mousemove;
		}
		if (!in_menu_loop) {
			int x = LOWORD(lparam);
			int y = HIWORD(lparam);
			toggle_flag((x+4)>>4, (y-39)>>4);
		}
		break;
	case WM_MOUSEMOVE:
mousemove:
		if (mouse_captured) {
			if (game_state) {
				int x = LOWORD(lparam);
				int y = HIWORD(lparam);
				slide_on_tile((x+4)>>4, (y-39)>>4);
			} else {
release_mouse:
				mouse_captured = 0;
				ReleaseCapture();
				if (game_state) {
					sub_10037e1();
				} else {
					slide_on_tile(-2, -2);
				}
			}
		}
		break;
	case WM_ACTIVATE:
		if (wparam == WA_CLICKACTIVE) just_activated = 1;
		break;
	case WM_TIMER:
		tick();
		return 0;
	case WM_COMMAND:
		cmd = LOWORD(wparam);
		switch (cmd) {
		case 510:
			new_game();
			break;
		case 512:
			SendMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
			break;
		case 521:
		case 522:
		case 523:
			set_board_type(cmd-521);
			new_game();
config_changed:
			check_menu_items();
			break;
		case 524:
			DialogBoxParam(the_instance, (LPCTSTR) 80, hwnd,
				       custom_field_dlgproc, 0);
			board_type = 3;
			check_menu_items();
			new_game();
			break;
		case 526:
			if (sound) {
				if (sound == 3) {
					PlaySound(0, 0, SND_PURGE);
				}
				sound = 0;
			}
			goto config_changed;
		case 527:
		case 528:
			theme = cmd-527;
			load_graphics();
			InvalidateRect(hwnd, 0, 0);
			goto config_changed;
		case 529:
			solver_enabled = !solver_enabled;
			goto config_changed;
		}
		return 0;
	case WM_ENTERMENULOOP:
		in_menu_loop = 1;
		break;
	case WM_EXITMENULOOP:
		in_menu_loop = 0;
		break;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void
init(void)
{
	srand(GetTickCount());
	cycaption1 = GetSystemMetrics(SM_CYCAPTION)+1;
	cymenu1 = GetSystemMetrics(SM_CYMENU)+1;
	cyborder1 = GetSystemMetrics(SM_CYBORDER)+1;
	cxborder1 = GetSystemMetrics(SM_CXBORDER)+1;
}

int
stop_sounds(void)
{
	return PlaySound(0, 0, SND_PURGE) ? 3 : 1;
}

void
load_config(void)
{
	set_board_type(2);
	sound = stop_sounds();
	solver_enabled = 1;
	theme = THEME_WIN2K;
}

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE _p, LPSTR cmdline, int show)
{
	WNDCLASS wc;
	HACCEL accel;
	HWND hwnd;
	MSG msg;

	the_instance = instance;
	init();
	if (show == SW_SHOWMINNOACTIVE) {
		window_hidden = 1;
	} else {
		window_hidden = show == SW_SHOWMINIMIZED;
	}
	wc.style = 0;
	wc.lpfnWndProc = wndproc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = instance;
	wc.hIcon = LoadIcon(instance, (LPCTSTR) 100);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH) GetStockObject(1);
	wc.lpszMenuName = 0;
	wc.lpszClassName = appname;
	if (!RegisterClass(&wc)) return 1;
	main_menu = LoadMenu(instance, (LPCTSTR) 500);
	accel = LoadAccelerators(instance, (LPCTSTR) 501);

	load_config();
	hwnd = CreateWindow(appname, appname, 0xca0000,
			    CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
			    0, 0, instance, 0);
	if (!hwnd) return 1;
	main_window = hwnd;
	update_window(1);
	init_graphics();
	load_graphics();
	update_menu();
	new_game();
	ShowWindow(hwnd, 1);
	UpdateWindow(hwnd);
	window_hidden = 0;

	while (GetMessage(&msg, 0, 0, 0)) {
		if (!TranslateAccelerator(hwnd, accel, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return msg.wParam;
}

int
sprite_stride(int w, int h)
{
	return h*(((4*w+31)>>3)&-4);
}

void
init_graphics(void)
{
	int i;
	HDC dc = GetDC(main_window);
	for (i=0; i<16; i++) {
		HDC compat_dc;
		HBITMAP compat_bmp;
		compat_dc = CreateCompatibleDC(dc);
		tile_dc[i] = compat_dc;
		compat_bmp = CreateCompatibleBitmap(dc, 16, 16);
		SelectObject(compat_dc, compat_bmp);
	}
	ReleaseDC(main_window, dc);

	shade_pen = CreatePen(0, 1, 0x808080);
}

void
load_graphics(void)
{
	HRSRC res;
	int stride;
	int offset;
	int i;

	/* load tiles */
	res = FindResource(the_instance, (LPCTSTR)(410+theme),
			   (LPCTSTR) RT_BITMAP);
	tile_bmpdata = (uchar *) LoadResource(the_instance, res);
	stride = sprite_stride(16, 16);
	offset = 0x68;
	for (i=0; i<16; i++) {
		SetDIBitsToDevice(tile_dc[i],
				  0, 0, 16, 16,
				  0, 0, 0, 16,
				  tile_bmpdata + offset,
				  (BITMAPINFO *) tile_bmpdata, 0);
		offset += stride;
	}

	/* load digits */
	res = FindResource(the_instance, (LPCTSTR) 420, (LPCTSTR) RT_BITMAP);
	digit_bmpdata = (uchar *) LoadResource(the_instance, res);
	offset = 0x68;
	stride = sprite_stride(13, 23);
	for (i=0; i<12; i++) {
		digit_bmpdata_offset[i] = offset;
		offset += stride;
	}

	/* load faces */
	res = FindResource(the_instance, (LPCTSTR)(430+theme),
			   (LPCTSTR) RT_BITMAP);
	face_bmpdata = (uchar *) LoadResource(the_instance, res);
	offset = 0x68;
	stride = sprite_stride(24, 24);
	for (i=0; i<5; i++) {
		face_bmpdata_offset[i] = offset;
		offset += stride;
	}
}
