#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BYTE_MAX     256  // 一個位元組共有 256 種數值
#define MAX_SYMB     3000  // 最多可記錄的符號種類

typedef struct {
    unsigned char chr[4];     // 符號的位元組內容
    int useLen;               // 符號長度：1～4 bytes
    int count;                // 符號出現次數
    double prob;              // 符號出現機率
} Symb;

//------------------ Big5 解碼 ------------------
static int big5_len(unsigned char b0){
    if (b0 <= 0x7F) return 1;                 // 第一個 byte 小於等於 0x7F，視為 ASCII
    if (b0 >= 0x81 && b0 <= 0xFE) return 2;   // Big5 第一個 byte 的範圍為 0x81～0xFE
    return 1;                                 
}
// Big5 第二個 byte 的範圍為 0x40～0x7E 或 0xA1～0xFE
static int is_big5_follow(unsigned char b1){
    return ( (b1 >= 0x40 && b1 <= 0x7E) || (b1 >= 0xA1 && b1 <= 0xFE) ); // 檢查是否符合 Big5 規則
}
//------------------ UTF-8 解碼 ------------------
static int utf8_len(unsigned char b0){  // 根據第一個 byte 判斷 UTF-8 符號長度
    if ((b0 & 0x80)==0x00) return 1; // 0xxxxxxx
    if ((b0 & 0xE0)==0xC0) return 2; // 110xxxxx
    if ((b0 & 0xF0)==0xE0) return 3; // 1110xxxx
    if ((b0 & 0xF8)==0xF0) return 4; // 11110xxx
    return 0;  
}

static int is_utf8_follow(unsigned char b){ // 檢查 UTF-8 後續 byte 是否符合 10xxxxxx
    return (b & 0xC0)==0x80; // 10xxxxxx
}

// qsort 使用的比較函式
static int cmp(const void *a, const void *b){ 
    const Symb *x = (const Symb*)a;
    const Symb *y = (const Symb*)b;
    if(x->count != y->count) 
        return y->count - x->count;   // 1. 出現次數由大到小
    if(x->useLen != y->useLen) 
        return x->useLen - y->useLen;     // 2. 符號長度由小到大
    return memcmp(x->chr, y->chr, x->useLen);  // 3. byte 值由小到大
}
// 將符號寫入輸出檔
static void csv_char(FILE *fout, const unsigned char *s,int len){
    if (len==1 && s[0]=='\r'){ fputs("\"\\r\"",fout); return; }
    if (len==1 && s[0]=='\n'){ fputs("\"\\n\"",fout); return; }
    if (len==1 && s[0]=='\t'){ fputs("\"\\t\"",fout); return; }
    fputc('"',fout); // 開頭的雙引號
    for(int i=0;i<len;i++){
        if (s[i] == '"') {
            fputc('"', fout);
            fputc('"', fout);
        }else {
            fputc(s[i],fout); // 輸出符號
        }
    }
    fputc('"',fout);// 結尾的雙引號
}

int main(int argc, char *argv[]){
    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.txt output.csv\n", argv[0]);
        return 1;
    }

    FILE *fin = fopen(argv[1], "rb");
    if (fin == NULL) {
        fprintf(stderr, "Cannot open input file.\n");
        return 1;
    }

    FILE *fout = fopen(argv[2], "wb");
    if (fout == NULL) {
        fprintf(stderr, "Cannot open output file.\n");
        fclose(fin);
        return 1;
    }

    // 檢查檔案開頭是否有 UTF-8 BOM
    unsigned char bom[3];
    size_t readLen = fread(bom, 1, sizeof(bom), fin);
    if (readLen != sizeof(bom) || bom[0] != 0xEF || bom[1] != 0xBB || bom[2] != 0xBF) {
        if (fseek(fin, 0, SEEK_SET) != 0) {
            fprintf(stderr, "Cannot read input file.\n");
            fclose(fin);
            fclose(fout);
            return 1;
        }
    }

    Symb symb[MAX_SYMB]={0};

    // 初始化所有 ASCII 符號，長度為 1 byte
    for(int i=0;i<=0x7F;i++){ // ASCII 範圍為 0～127
        symb[i].chr[0]=(unsigned char)i; 
        symb[i].useLen=1; 
        symb[i].count=0; 
    }

    int used = BYTE_MAX;  // symb[] 已使用的數量，從 256 開始
    int total = 0;  // 符號總數
    int c;
    while((c=fgetc(fin))!=EOF){
        unsigned char b0 = (unsigned char)c; // 符號的第一個 byte
        // ----- 處理 ASCII 符號 -----
        if(b0 <= 0x7F){ // ASCII 只有一個 byte，前面已初始化
            symb[b0].count++; 
            total++; 
            continue;
        }

        // 以下處理非 ASCII 符號
        int symbLen=1; 
        unsigned char tmp[4]; 
        tmp[0]=b0; // 第一個 byte
        int uLen=0; // 符號長度
        int read=1, ok=0; // 檢查後續 byte 並存入 tmp[]

        // --------- 嘗試處理 UTF-8 ---------
        uLen = utf8_len(b0); // 檢查 UTF-8 第一個 byte 並取得長度
        if(uLen > 1){
            ok = 1;
            for(int i=1;i<uLen;i++){
                int d=fgetc(fin);
                if(d==EOF){ ok=0; break; } // 到達檔案結尾
                tmp[read++]=(unsigned char)d; // 讀取後續 byte
                if(!is_utf8_follow(tmp[read-1])){ ok=0; break; } // UTF-8 後續 byte 不合法
            }
        }
        if (ok){ // 確認為 UTF-8
            symbLen = uLen; 
        } else { // 不是 UTF-8，將後續 byte 放回檔案再檢查 Big5
            for (int j=read-1;j>=1; --j) ungetc(tmp[j], fin); // 保留 b0，只放回後續 byte
        }

        // --------- 嘗試處理 Big5 ---------
        if (symbLen == 1){ // 不是 UTF-8 時才檢查 Big5
            uLen = big5_len(b0); // 檢查 Big5 第一個 byte 並取得長度
            if (uLen == 2){
                int d = fgetc(fin);
                if (d != EOF && is_big5_follow((unsigned char)d)){ // 檢查第二個 byte 是否合法
                    tmp[1] = (unsigned char)d;
                    symbLen = 2;   // 確認為 Big5
                } else {
                    if (d != EOF) ungetc(d, fin); // 不合法時將讀到的 byte 放回
                }
            }
        }

        // ---------- 處理其他未知的單一 byte ----------
        if (symbLen == 1){ // 不符合 UTF-8 或 Big5
            if (symb[b0].useLen == 0){  // 第一次使用這個索引
                symb[b0].useLen = 1;
                symb[b0].chr[0] = b0;
            }
            symb[b0].count++;
            total++;
            continue;
        }


        // --------- 將合法符號加入 symb[] ----------
        // 檢查符號是否已存在，存在就將次數加一
        int found=0;
        for(int i=BYTE_MAX;i<used;i++){ // 使用線性搜尋檢查符號是否存在
            // 長度相同且 byte 內容相同才是同一個符號
            if(symb[i].useLen==symbLen && memcmp(symb[i].chr,tmp,symbLen)==0){
            symb[i].count++; 
            found=1; 
            break; 
            }
        }
        // 新符號加入 symb[]，並將次數設為 1
        if(!found && used<MAX_SYMB){
            memcpy(symb[used].chr,tmp,symbLen); // 儲存符號
            symb[used].useLen=symbLen; // 儲存符號長度
            symb[used].count=1; // 初始出現次數
            used++; // 已使用的符號數量加一
        }
        total++; 
    }

    // ----------- 篩選有出現過的符號 ----------
    Symb out[MAX_SYMB]; // 輸出用陣列
    int n=0;
    for(int i=0;i<used;i++) {
        if(symb[i].count>0){ 
            out[n]=symb[i]; 
            n++; 
        }
    }
    // 計算每個符號的出現機率
    for(int i=0;i<n;i++){
        if(total>0){ // 避免除以零
            out[i].prob = (double)out[i].count/(double)total;
        }else{
            out[i].prob = 0.0;
        }
    }
    // 排序並輸出結果
    qsort(out,n,sizeof(Symb),cmp); // 使用 cmp 進行排序
    for(int i=0;i<n;i++){
        csv_char(fout,out[i].chr,out[i].useLen); // 輸出符號欄位
        fprintf(fout,",%d,%.15f\n", out[i].count, out[i].prob); // 輸出次數與機率
    }

    fclose(fin);
    if (fclose(fout) != 0) {
        fprintf(stderr, "Cannot write output file.\n");
        return 1;
    }
    return 0;
}
