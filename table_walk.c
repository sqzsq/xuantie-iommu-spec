#define DTE_V_BIT (1U << 0) 
#define DTE_S_BIT (1U << 1) 
#define DTE_F_BIT (1U << 2) 
u64 rsiddiv_reg;
u64 dtbase_reg;

void translate(rsid_t rsid, u64 input_addr, at_t access_type, u64* output_addr)
{
    u64 rsid_hi;
    u64 rsid_lo;
    u64 out_dte;
    u64 dtbase; rsiddiv
    int do_stage_one = do_stage_two = 0;
    u64 s1_atp, s2_atp, dev_conf;
    int res;

    dtbase = dtbase_reg;
    rsiddiv = rsiddiv_reg;

    if (rsiddiv)
    {
        u64 rsid_hi = rsid >> rsiddiv;
        u64 rsid_lo = rsid & ((1 << rsiddiv) - 1);
        u64 lv1_dte_addr = dtbase + (rsid_hi << 3);
        u64 lv1_dte = memory_load8 (lv1_dte_addr);

        if (lv1_dte & 0x7)
        {
            raise_exception(EX_RESERVED_BIT_SET, lv1_dte);
            return;
        }

        u64 lv2_dte_addr = lv1_dte + (rsid_lo << 3);

        out_dte =  memory_load8 (lv2_dte_addr);
    }
    else
    {
        u64 dte_addr = dtbase + (rsid << 3);
        out_dte = memory_load8 (dte_addr);
    }

    if(out_dte & DTE_V_BIT)
    {
        *output_addr = input_addr;
        return;
    }

    u64 desc_addr = out_dte & ~0x7;
    dev_conf = memory_load8 (desc_addr + 16);

    if(out_dte & DTE_F_BIT)
    {
        do_stage_one = 1;
        s1_atp = memory_load8 (desc_addr);
    }

    if(out_dte & DTE_S_BIT)
    {
        do_stage_two = 1;
        s2_atp = memory_load8 (desc_addr + 8);
    }

    if(!do_stage_two)
    {
        if(!do_stage_one)
        {
            if (s1_atp & (0xF << 60)) /* S1MODE is 0 */
            {
                // do nothing for now
                // raise_exception(EX_S1MODE_INCONSISTENT, desc_addr);
                // return;
            }
            *output_addr = input_addr;
            return;
        }
        else /* Stage-2 disabled, Stage-1 enabled */
        {
            u64 exp_addr;
            *output_addr = riscv_one_stage(s1_atp, input_addr, access_type, &res, &exp_addr);
            if(!res)
            {
                raise_exception(res, exp_addr);
            }
            return;
        }
    }
    else
    {
        if(!do_stage_one) /* Stage-2 enabled, stage-1 disabled */
        {
            u64 exp_addr;
            *output_addr = riscv_one_stage(s2_atp, input_addr, access_type, &res, &exp_addr);
            if(!res)
            {
                raise_exception(res, exp_addr);
            }
            return;
        }
        else /* Stage-2 enabled, stage-1 enabled */
        {
            *output_addr = riscv_two_stages(s1_atp, s2_atp, input_addr, access_type, &res, &exp_addr);
            if(!res)
            {
                raise_exception(res, exp_addr);
            }
        }
    }

    return;
}
