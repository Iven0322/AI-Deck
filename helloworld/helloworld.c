/* PMSIS includes */
#include "pmsis.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#define MATRIX_SIZE 100
#define CORE_NUM 8

struct pi_device cluster_dev;  //定义了对集群的表示，包括对集群的使用方法配置等等，主要表示对集群的控制
struct pi_cluster_conf cl_conf;//定义了集群的配置参数信息，包括id控制栈大小等等
int rows;
int cols;
int matrix[MATRIX_SIZE][MATRIX_SIZE];
//存储矩阵最大值的结构体
typedef struct {
    int local_max[8];  // 假设有8个核心，每个核心的局部最大值
    int global_max;    // 所有核心搜索到的全局最大值
} max_values_t;
max_values_t max_values;
max_values_t max_values = {.global_max = INT_MIN};  // 使用INT_MIN初始化全局最大值，保证任何元素都大于它



/* Task executed by cluster cores. */
//进行矩阵搜索的任务函数
void search_max_in_row(void *arg) {
    // 计算当前核心的ID，确定要处理的矩阵行
    int core_id = pi_core_id();
    // printf(max_values.global_max);
    max_values.local_max[core_id] = INT_MIN;
    
    for (int i = 0; i < MATRIX_SIZE; i++) {
        if (matrix[core_id][i] > max_values.local_max[core_id]) {
            max_values.local_max[core_id] = matrix[core_id][i];
        }
    }

    // 使用原子操作更新全局最大值
    pi_cl_team_critical_enter();
    if (max_values.local_max[core_id] > max_values.global_max) {
        max_values.global_max = max_values.local_max[core_id];
    }
    pi_cl_team_critical_exit();
    
}

//调用内存的做法
void serch_matrix(void *arg){
    int core_id=pi_core_id();
    int total_rows = rows; // 矩阵总行数
    int rows_per_core = (total_rows + CORE_NUM - 1) / CORE_NUM; // 每个核心处理的行数，向上取整
    int start_row = core_id * rows_per_core; // 该核心处理的起始行
    int end_row = start_row + rows_per_core; // 结束行（不包括）
    if (end_row > total_rows) end_row = total_rows; // 处理行数溢出的情况

    int rows_to_process = end_row - start_row;
    pi_cl_team_critical_enter();//使用临界区来解决L1内存分配冲突问题
    int *row_data = (int *)pi_cl_l1_malloc(&cluster_dev,rows_to_process * sizeof(int) * cols);
        if (row_data == NULL) {
        // 错误处理: L1内存分配失败
        return;
    }
    pi_cl_team_critical_exit();
    // printf(max_values.global_max);
    //    // 从L2内存复制矩阵行到L1内存
    // pi_cl_dma_cmd_t dma_copy;
    // pi_cl_dma_cmd((uint32_t)&matrix[row][0], (uint32_t)row_data, MATRIX_SIZE * sizeof(int), PI_CL_DMA_DIR_EXT2LOC, &dma_copy);
    // pi_cl_dma_wait(&dma_copy);

    
    //  max_values.local_max[core_id] = INT_MIN;
    // //     在L1内存中搜索最大值
    // for (int i = 0; i < MATRIX_SIZE; i++) {
    //     if (row_data[i] > max_values.local_max[core_id]) {
    //         max_values.local_max[core_id] = row_data[i];
    //     }
    // }

    for (int row = start_row; row < end_row; row++) {
        pi_cl_dma_cmd_t dma_copy;
        pi_cl_dma_cmd((uint32_t)&matrix[row][0], (uint32_t)(row_data + (row - start_row) * cols), cols * sizeof(int), PI_CL_DMA_DIR_EXT2LOC, &dma_copy);
        pi_cl_dma_wait(&dma_copy);

        for (int i = 0; i < cols; i++) {
            if (row_data[(row - start_row) * cols + i] > max_values.local_max[core_id]) {
                max_values.local_max[core_id] = row_data[(row - start_row) * cols + i];
            }
        }
    }

    // 更新全局最大值...
    pi_cl_team_critical_enter();
    if (max_values.local_max[core_id]> max_values.global_max) {
        max_values.global_max = max_values.local_max[core_id];
    }
    pi_cl_team_critical_exit();
    //释放L1内存
    pi_cl_l1_free(&cluster_dev,row_data, rows_to_process * sizeof(int) * cols);

}


void cluster_helloworld(void *arg)
{
    uint32_t core_id = pi_core_id(), cluster_id = pi_cluster_id();
    //generateAndPrintRandomMatrix(8);//生成一个8*8的矩阵

    // if(core_id==0){
    //      printf("[%d %d] iven\n", cluster_id, core_id);
    // }
    // else if(core_id==1){
    //     printf("[%d %d] xie\n", cluster_id, core_id);
    // }
    // else{
    //     printf("[%d %d] We are empty!\n", cluster_id, core_id);
    // }

    //printf("[%d %d] iven\n", cluster_id, core_id);
}


unsigned int seed = 1234; // 全局种子
// 简单的线性同余生成器，仅作示例，实际应用可能需要更复杂的算法
int my_rand() {
    seed = seed * 15 + 1234567;
    return (unsigned int)(seed/65536) % 32768;
}

// 函数定义：生成并打印随机大小的随机数矩阵
void generateAndPrintRandomMatrix(int rows,int cols) {
    int i, j; // 循环变量

    // 生成并打印size x size的随机数矩阵
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            matrix[i][j] = my_rand() % 1000; // 生成0到99之间的随机数
             printf("%d\t", matrix[i][j]);
        }
        printf("\n");
    }
}
/* Cluster main entry, executed by core 0. */
void cluster_delegate(void *arg)
{
    printf("Cluster master core entry\n");
    /* Task dispatch to cluster cores. */
    pi_cl_team_fork(pi_cl_cluster_nb_cores(), serch_matrix, arg);
    printf("Cluster master core exit\n");
}

void helloworld(void){
    printf("Entering main controller\n");

    uint32_t errors = 0;

    /* Init cluster configuration structure. */
    pi_cluster_conf_init(&cl_conf);//初始化集群的参数
    cl_conf.id = 0;//设置集群ID                /* Set cluster ID. */
    /* Configure & open cluster. */
    pi_open_from_conf(&cluster_dev, &cl_conf);//将设备信息与设备连接，在后续对设备进行操作时可以使用这些信息。
    if (pi_cluster_open(&cluster_dev)) //打开集群，如果返回值不为0，表示打开失败，打印信息，并退出系统
    {
        printf("Cluster open failed !\n");
        pmsis_exit(-1);
    }

    struct pi_cluster_task cl_task;

    pi_cluster_send_task_to_cl(&cluster_dev, pi_cluster_task(&cl_task, cluster_delegate, NULL));

    pi_cluster_close(&cluster_dev);
    printf("Global maximum value is: %d\n", max_values.global_max);
    printf("Test success !\n");

    pmsis_exit(errors);
}
/* Program Entry. */
int main(void)
{

    printf("\n\n\t *** PMSIS HelloWorld ***\n\n");
    rows = 8 + (my_rand() % 100);
    cols = 8 + (my_rand() % 100);
    generateAndPrintRandomMatrix(rows,cols);
    return pmsis_kickoff((void *) helloworld);
}



